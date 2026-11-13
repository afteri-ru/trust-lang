# cmake/check_component_deps.cmake
# CI-хранитель соответствия ФАКТИЧЕСКИХ зависимостей кода и ОБЪЯВЛЕННЫХ зависимостей сборки.
#
# Правила:
#   A) каждый #include "<B>/..." в include/A/** обязан покрываться ПУБЛИЧНЫМ link-замыканием A
#      (заголовки A не могут опираться на PRIVATE-зависимость); тот же include в src/A/**
#      покрывается полным link-замыканием A (PUBLIC/PRIVATE + их публичные хвосты);
#   B) ребро A->B, не подкреплённое ни одним #include "<B>/..." в include/A/** или src/A/**,
#      запрещено (мёртвая зависимость: MEMORY о компоненте перестаёт соответствовать сборке);
#   C) ребро A->B с видимостью PUBLIC обязано быть использовано в ПУБЛИЧНЫХ заголовках A —
#      иначе оно должно быть PRIVATE (не протекать в интерфейс).
#
# Источник манифеста — ЕДИНЫЙ парсер cmake/component_manifest.cmake (trust_component(...)):
# LINK_PUBLIC/LINK_PRIVATE компонентной цели берутся из манифеста, дополнительные цели
# (исполняемые, условные) — из literal add_library/add_executable и их target_link_libraries.
#
# Видимость моделируется как в C++: PRIVATE-зависимость видна только владельцу, транзитивно
# доступны лишь PUBLIC/INTERFACE-рёбра.
#
# Использование: cmake -DROOT=<корень> -P cmake/check_component_deps.cmake
# Возврат: 0 - нарушений нет, 1 - есть нарушения.

include("${CMAKE_CURRENT_LIST_DIR}/guard_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/component_manifest.cmake")

guard_begin(component_deps_guard)

# Известные отклонения "компонент->компонент": код опирается на заголовок, ребра сборки нет
# (или ребро есть без использования). Каждая запись обоснована; устранение нарушения слоя —
# отдельная задача, а не повод отключить гейт.
set(_known_exceptions
    "types->runtime"     # type_id.hpp/typekind.hpp: runtime/trust/type_state.hpp — единое ABI-определение (MEMORY runtime)
    "runtime->stdlib"    # заголовки встроенных типов (stdlib/core/builtin/*.hpp) и провайдера tensor (stdlib/tensor/*.hpp)
                         # компилируются рантаймом (rational/big_integer/tensor + torch-адаптер) как C++-TU
    "package->runtime"       # package.cpp: [build].runtime-abi — версия ABI из единого ABI-заголовка (E14/R48)
    "module_loader->runtime" # package_store.cpp: сверка runtime-abi пакета с версией компилятора (R48)
)

# Запрещённые рёбра: семантическая граница слоёв (E20/R34). Ребро недопустимо ДАЖЕ если подкреплено
# include — границу нельзя восстанавливать «случайной» ссылкой. `pipeline` — конвейер компиляции,
# `toolchain` — сборка хостовым компилятором; конвейер от последнего не зависит (сборка — в `builddriver`).
set(_forbidden_edges
    "pipeline->toolchain"
    "ast->session"    # AST-слой не зависит от фасада сессии: порты ConvertServices (R34)
    "syntax->session" # парсер не зависит от фасада сессии: порт ParserServices (R34)
)

guard_components(_components)
if(NOT _components)
    message(FATAL_ERROR "component_deps_guard: не найдено ни одного компонента в ${ROOT}/include")
endif()

# Провайдеры заголовков: корень include-пути считается зависимостью только если компонент
# реально предоставляет заголовки. `trust` — пространство имён пакета (генерируемый
# trust/version.h и ABI-заголовки runtime/trust), а не компонент с публичными заголовками.
set(_provider_components "")
foreach(_component IN LISTS _components)
    guard_files(_comp_headers ROOTS "${ROOT}/include/${_component}" GLOBS "*.hpp" "*.h")
    if(_comp_headers)
        list(APPEND _provider_components "${_component}")
    endif()
endforeach()

# -- 1. Компоненты: манифест trust_component(...) + дополнительные цели ---------------
foreach(_component IN LISTS _components)
    set(_cmake "${ROOT}/include/${_component}/CMakeLists.txt")
    file(READ "${_cmake}" _comp_text)
    trust_manifest_parse("${_cmake}" M)

    set(_targets "")
    set(_manifest_target "")
    if(M_NO_LIB STREQUAL "")
        if(M_TARGET STREQUAL "")
            set(_manifest_target "${_component}_lib")
        else()
            set(_manifest_target "${M_TARGET}")
        endif()
        list(APPEND _targets "${_manifest_target}")
    elseif(NOT M_TARGET STREQUAL "")
        set(_manifest_target "${M_TARGET}")
        list(APPEND _targets "${_manifest_target}")
    endif()

    # Цели, объявленные вне trust_component (исполняемые/условные/флейворы).
    foreach(_kind IN ITEMS add_library add_executable)
        string(REGEX MATCHALL "${_kind}[(][ ]*[A-Za-z0-9_-]+" _decls "${_comp_text}")
        foreach(_decl IN LISTS _decls)
            string(REPLACE "${_kind}(" "" _target "${_decl}")
            string(STRIP "${_target}" _target)
            list(APPEND _targets "${_target}")
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _targets)
    set(_targets_of_${_component} "${_targets}")
    foreach(_target IN LISTS _targets)
        set(_component_of_${_target} "${_component}")
    endforeach()

    # -- 2. Рёбра: манифест (LINK_PUBLIC/LINK_PRIVATE) ---------------------------------
    if(NOT _manifest_target STREQUAL "")
        foreach(_dep IN LISTS M_LINK_PUBLIC)
            list(APPEND _edges_${_manifest_target} "${_dep}|PUBLIC")
        endforeach()
        foreach(_dep IN LISTS M_LINK_PRIVATE)
            list(APPEND _edges_${_manifest_target} "${_dep}|PRIVATE")
        endforeach()
    endif()

    # -- 2b. Рёбра: literal target_link_libraries(<owner> [VIS] <dep>...) ----------------
    string(REGEX MATCHALL "target_link_libraries[(][^)]*[)]" _link_calls "${_comp_text}")
    foreach(_call IN LISTS _link_calls)
        string(REPLACE "target_link_libraries(" "" _inner "${_call}")
        string(REPLACE ")" "" _inner "${_inner}")
        string(REPLACE "\n" " " _inner_spaced "${_inner}")
        separate_arguments(_tokens UNIX_COMMAND "${_inner_spaced}")
        list(LENGTH _tokens _token_count)
        if(_token_count LESS 2)
            continue()
        endif()
        list(GET _tokens 0 _owner)
        if(_owner MATCHES "^[$]")
            continue()  # цель-переменная (флейворы runtime) вне модели компонентов
        endif()

        set(_visibility PUBLIC)
        list(REMOVE_AT _tokens 0)
        foreach(_token IN LISTS _tokens)
            if(_token STREQUAL "PUBLIC" OR _token STREQUAL "PRIVATE" OR _token STREQUAL "INTERFACE")
                set(_visibility "${_token}")
                continue()
            endif()
            if(_token MATCHES "^[$]")
                continue()  # подстановка CMake, не имя цели
            endif()
            list(APPEND _edges_${_owner} "${_token}|${_visibility}")
        endforeach()
    endforeach()
endforeach()

# -- 3. Замыкания зависимостей ----------------------------------------------------------
# component_closure(<out> <component> <mode>): компоненты, чьи заголовки доступны компоненту.
# mode=iface — только PUBLIC/INTERFACE-рёбра (видимость из публичных заголовков);
# mode=impl  — прямые зависимости ЛЮБОЙ видимости, далее только их публичный хвост.
function(component_closure out_var component mode)
    set(_reached "")
    set(_visited_targets "")
    set(_queue "")
    set(_targets_key "_targets_of_${component}")
    if(mode STREQUAL "iface")
        set(_queue ${${_targets_key}})
    else()
        foreach(_target IN LISTS ${_targets_key})
            set(_edge_key "_edges_${_target}")
            foreach(_edge IN LISTS ${_edge_key})
                string(REPLACE "|" ";" _edge_parts "${_edge}")
                list(GET _edge_parts 0 _dep_target)
                set(_dep_key "_component_of_${_dep_target}")
                if(DEFINED ${_dep_key})
                    list(APPEND _reached "${${_dep_key}}")
                    list(APPEND _queue "${_dep_target}")
                endif()
            endforeach()
        endforeach()
    endif()

    while(_queue)
        list(POP_FRONT _queue _target)
        list(FIND _visited_targets "${_target}" _seen_index)
        if(_seen_index GREATER -1)
            continue()
        endif()
        list(APPEND _visited_targets "${_target}")

        set(_edge_key "_edges_${_target}")
        foreach(_edge IN LISTS ${_edge_key})
            string(REPLACE "|" ";" _edge_parts "${_edge}")
            list(GET _edge_parts 0 _dep_target)
            list(GET _edge_parts 1 _dep_visibility)
            if(mode STREQUAL "iface" AND _dep_visibility STREQUAL "PRIVATE")
                continue()
            endif()
            set(_dep_key "_component_of_${_dep_target}")
            if(NOT DEFINED ${_dep_key})
                continue()  # внешняя/некомпонентная цель
            endif()
            list(APPEND _reached "${${_dep_key}}")
            list(APPEND _queue "${_dep_target}")
        endforeach()
    endwhile()
    list(REMOVE_DUPLICATES _reached)
    set(${out_var} "${_reached}" PARENT_SCOPE)
endfunction()

# -- 4. Проверки ------------------------------------------------------------------------
foreach(_component IN LISTS _components)
    component_closure(_iface_deps "${_component}" iface)
    component_closure(_impl_deps "${_component}" impl)

    # A) каждый include покрыт link-замыканием компонента.
    set(_included_all "")
    set(_included_public "")
    foreach(_area IN ITEMS include src)
        set(_dir "${ROOT}/${_area}/${_component}")
        if(NOT IS_DIRECTORY "${_dir}")
            continue()
        endif()
        guard_include_edges(_edges "${_dir}")
        foreach(_edge IN LISTS _edges)
            string(REPLACE "|" ";" _parts "${_edge}")
            list(GET _parts 0 _rel)
            list(GET _parts 1 _inc)
            string(REPLACE "/" ";" _segments "${_inc}")
            list(LENGTH _segments _seg_count)
            if(_seg_count LESS 2)
                continue()
            endif()
            list(GET _segments 0 _inc_root)
            if(_inc_root STREQUAL "")
                continue()
            endif()
            list(FIND _provider_components "${_inc_root}" _is_component)
            if(_is_component EQUAL -1)
                continue()  # системный/внешний заголовок
            endif()
            list(FIND _included_all "${_inc_root}" _already)
            if(_already EQUAL -1)
                list(APPEND _included_all "${_inc_root}")
            endif()
            if(_area STREQUAL "include")
                list(FIND _included_public "${_inc_root}" _already_pub)
                if(_already_pub EQUAL -1)
                    list(APPEND _included_public "${_inc_root}")
                endif()
            endif()
            if(_inc_root STREQUAL _component)
                continue()
            endif()
            list(FIND _known_exceptions "${_component}->${_inc_root}" _known_exception)
            if(_known_exception GREATER -1)
                continue()
            endif()
            set(_scope "полное")
            set(_deps "${_impl_deps}")
            if(_area STREQUAL "include")
                set(_scope "публичное")
                set(_deps "${_iface_deps}")
            endif()
            list(FIND _deps "${_inc_root}" _covered)
            if(_covered EQUAL -1)
                guard_violation("${_rel}: '${_inc}' требует ${_scope} ребро '${_component}' -> '${_inc_root}'")
            endif()
        endforeach()
    endforeach()

    # B/C) рёбра компонента подкреплены include-ами
    set(_component_edges "")
    set(_targets_key "_targets_of_${_component}")
    foreach(_target IN LISTS ${_targets_key})
        set(_edge_key "_edges_${_target}")
        list(APPEND _component_edges ${${_edge_key}})
    endforeach()
    list(REMOVE_DUPLICATES _component_edges)
    foreach(_edge IN LISTS _component_edges)
        string(REPLACE "|" ";" _edge_parts "${_edge}")
        list(GET _edge_parts 0 _dep_target)
        list(GET _edge_parts 1 _dep_visibility)
        set(_owner_key "_component_of_${_dep_target}")
        if(NOT DEFINED ${_owner_key})
            continue()
        endif()
        set(_dep_component "${${_owner_key}}")
        if(_dep_component STREQUAL _component)
            continue()  # ребро внутри компонента (например tensor_cpu -> runtime_lib)
        endif()
        list(FIND _forbidden_edges "${_component}->${_dep_component}" _forbidden_edge)
        if(_forbidden_edge GREATER -1)
            guard_violation("${_component}: запрещённое ребро -> '${_dep_component}' (${_dep_visibility}): граница слоёв (E20/R34)")
            continue()
        endif()
        list(FIND _known_exceptions "${_component}->${_dep_component}" _known_edge)
        if(_known_edge GREATER -1)
            continue()
        endif()

        list(FIND _included_all "${_dep_component}" _used)
        if(_used EQUAL -1)
            guard_violation("${_component}: ребро -> '${_dep_component}' (${_dep_visibility}) не используется ни одним include")
            continue()
        endif()

        if(_dep_visibility STREQUAL "PUBLIC")
            list(FIND _included_public "${_dep_component}" _used_public)
            if(_used_public EQUAL -1)
                guard_violation("${_component}: PUBLIC ребро -> '${_dep_component}' не используется в публичных заголовках (нужно PRIVATE)")
            endif()
        endif()
    endforeach()
endforeach()

guard_end("зависимости кода не соответствуют зависимостям сборки (см. cmake/check_component_deps.cmake)")
