# cmake/component.cmake
# Единый конструктор библиотечной цели компонента и проверка манифеста компонента.
#
# КОМПОНЕНТ = каталог include/<component>/ (см. шапку корневого CMakeLists.txt).
# Публичный API компонента — ТОЛЬКО заголовки в корне include/<component>/; они объявляются
# в манифесте (PUBLIC_HEADERS) и включаются другими компонентами.
# Детали реализации группируются в internal-подкаталогах (INTERNAL_DIRS, аналог internal/ в Go):
#   include/<component>/<internal-dir>/ + src/<component>/<internal-dir>/ + MEMORY.md.
# Internal-подкаталог может существовать только в src/ (реализация или ресурсы без заголовков);
# объявленный подкаталог рекурсивен и классифицируется по самому длинному префиксу
# (cmake/internal_dirs.cmake). Заголовки internal-подкаталогов недоступны другим компонентам
# (CI-гейты internal_boundary_guard и subdir_layout_guard).
#
# trust_component(<component>
#     [TARGET <name>]                    # по умолчанию <component>_lib
#     [INTERFACE]                        # header-only: цель INTERFACE, SOURCES запрещены
#     [NO_LIB]                           # только манифест/проверки (tool-компонент без библиотеки)
#     PUBLIC_HEADERS <hpp...>            # имена файлов в корне include/<component>/
#     PUBLIC_DIRS   <dir...>             # намеренно публичные вложенные каталоги (напр. runtime/trust)
#     INTERNAL_DIRS <dir...>             # подкаталоги деталей реализации: include/<c>/<dir> и/или src/<c>/<dir>
#     SOURCES <relative-cpp...>          # пути относительно src/<component>/ (абсолютные допустимы)
#     LINK_PUBLIC <target...>  LINK_PRIVATE <target...>
#     INCLUDE_PUBLIC <dir...>  INCLUDE_PRIVATE <dir...>  SYSTEM_INCLUDE_PRIVATE <dir...>
#                                        # INCLUDE_PUBLIC не может указывать на корень компонента
#     COMPILE_OPTIONS <opt...>  PUBLIC_COMPILE_OPTIONS <opt...>
#     DEPENDS <target...>)               # add_dependencies

include_guard(GLOBAL)

function(trust_component _name)
    cmake_parse_arguments(C
        "INTERFACE;NO_LIB"
        "TARGET"
        "PUBLIC_HEADERS;PUBLIC_DIRS;INTERNAL_DIRS;SOURCES;LINK_PUBLIC;LINK_PRIVATE;INCLUDE_PUBLIC;INCLUDE_PRIVATE;SYSTEM_INCLUDE_PRIVATE;COMPILE_OPTIONS;PUBLIC_COMPILE_OPTIONS;DEPENDS"
        ${ARGN})

    if(NOT C_TARGET)
        set(C_TARGET "${_name}_lib")
    endif()

    set(_comp_include_dir "${PROJECT_INCLUDE_DIR}/${_name}")
    set(_comp_src_dir "${TRUST_SRC_DIR}/${_name}")

    if(NOT IS_DIRECTORY "${_comp_include_dir}")
        message(FATAL_ERROR "trust_component(${_name}): каталог include/${_name}/ не найден")
    endif()

    # -- Манифест: публичные заголовки -------------------------------------
    foreach(_hdr IN LISTS C_PUBLIC_HEADERS)
        if(NOT EXISTS "${_comp_include_dir}/${_hdr}")
            message(FATAL_ERROR
                "trust_component(${_name}): PUBLIC_HEADERS '${_hdr}' отсутствует в include/${_name}/ "
                "(публичный API — только заголовки корня компонента)")
        endif()
    endforeach()

    # -- Манифест: подкаталоги ---------------------------------------------
    # Публичный подкаталог — всегда заголовки: include/<c>/<dir> обязан существовать.
    foreach(_dir IN LISTS C_PUBLIC_DIRS)
        if(NOT IS_DIRECTORY "${_comp_include_dir}/${_dir}")
            message(FATAL_ERROR "trust_component(${_name}): публичный подкаталог '${_dir}' отсутствует в include/${_name}/")
        endif()
    endforeach()
    # Internal-подкаталог может быть header-only (include/<c>/<dir>), impl-only или ресурсным
    # (src/<c>/<dir>): достаточно одного из двух.
    foreach(_dir IN LISTS C_INTERNAL_DIRS)
        if(NOT IS_DIRECTORY "${_comp_include_dir}/${_dir}" AND NOT IS_DIRECTORY "${_comp_src_dir}/${_dir}")
            message(FATAL_ERROR "trust_component(${_name}): internal-подкаталог '${_dir}' отсутствует "
                                "и в include/${_name}/, и в src/${_name}/")
        endif()
    endforeach()

    # Корень компонента нельзя публиковать как include-путь: иначе internal-подкаталоги
    # становятся включаемыми короткой формой через другую цель (обход internal_boundary_guard).
    foreach(_inc IN LISTS C_INCLUDE_PUBLIC)
        get_filename_component(_inc_abs "${_inc}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(_inc_abs STREQUAL _comp_include_dir)
            message(FATAL_ERROR "trust_component(${_name}): INCLUDE_PUBLIC не может указывать на корень "
                                "компонента (${_inc}) — публикуйте только include/ через trust_include_dirs")
        endif()
    endforeach()


    # -- Источники: существование и уникальное владение --------------------
    set(_srcs "")
    foreach(_src IN LISTS C_SOURCES)
        if(IS_ABSOLUTE "${_src}")
            set(_abs "${_src}")
        else()
            set(_abs "${_comp_src_dir}/${_src}")
        endif()
        if(NOT EXISTS "${_abs}")
            message(FATAL_ERROR "trust_component(${_name}): источник '${_src}' не найден")
        endif()
        file(RELATIVE_PATH _rel "${TRUST_SRC_DIR}" "${_abs}")
        trust_claim_sources("${_rel}")
        list(APPEND _srcs "${_abs}")
    endforeach()

    # -- Цель --------------------------------------------------------------
    if(C_INTERFACE AND _srcs)
        message(FATAL_ERROR "trust_component(${_name}): INTERFACE-компонент не может иметь SOURCES")
    endif()

    if(C_NO_LIB)
        if(_srcs OR C_LINK_PUBLIC OR C_LINK_PRIVATE)
            message(FATAL_ERROR "trust_component(${_name}): NO_LIB не сочетается с SOURCES/LINK_*")
        endif()
        return()
    endif()

    if(C_INTERFACE)
        if(C_LINK_PRIVATE)
            message(FATAL_ERROR "trust_component(${_name}): INTERFACE-компонент не может иметь LINK_PRIVATE")
        endif()
        if(C_INCLUDE_PRIVATE OR C_SYSTEM_INCLUDE_PRIVATE)
            message(FATAL_ERROR "trust_component(${_name}): INTERFACE-компонент не может иметь INCLUDE_PRIVATE")
        endif()
        add_library(${C_TARGET} INTERFACE)
        target_link_libraries(${C_TARGET} INTERFACE ${C_LINK_PUBLIC})
        if(C_INCLUDE_PUBLIC)
            target_include_directories(${C_TARGET} INTERFACE ${C_INCLUDE_PUBLIC})
        endif()
        if(C_COMPILE_OPTIONS)
            target_compile_options(${C_TARGET} INTERFACE ${C_COMPILE_OPTIONS})
        endif()
        if(C_PUBLIC_COMPILE_OPTIONS)
            target_compile_options(${C_TARGET} INTERFACE ${C_PUBLIC_COMPILE_OPTIONS})
        endif()
        return()
    endif()

    add_library(${C_TARGET} STATIC ${_srcs})

    target_include_directories(${C_TARGET} PUBLIC ${C_INCLUDE_PUBLIC})
    target_include_directories(${C_TARGET} PRIVATE ${C_INCLUDE_PRIVATE})
    target_include_directories(${C_TARGET} SYSTEM PRIVATE ${C_SYSTEM_INCLUDE_PRIVATE})

    target_link_libraries(${C_TARGET} PUBLIC ${C_LINK_PUBLIC})
    target_link_libraries(${C_TARGET} PRIVATE ${C_LINK_PRIVATE})

    if(C_COMPILE_OPTIONS)
        target_compile_options(${C_TARGET} PRIVATE ${C_COMPILE_OPTIONS})
    endif()
    if(C_PUBLIC_COMPILE_OPTIONS)
        target_compile_options(${C_TARGET} PUBLIC ${C_PUBLIC_COMPILE_OPTIONS})
    endif()

    if(C_DEPENDS)
        add_dependencies(${C_TARGET} ${C_DEPENDS})
    endif()
endfunction()


# trust_claim_sources(<relative-cpp...>)
# Помечает TU как принадлежащий цели, объявленной ВНЕ trust_component (исполняемый файл,
# условная цель вроде trust-dap / tensor_cpu / runtime-флейворы). Пути — относительно src/.
function(trust_claim_sources)
    foreach(_rel IN LISTS ARGN)
        if(NOT EXISTS "${TRUST_SRC_DIR}/${_rel}")
            message(FATAL_ERROR "trust_claim_sources: источник '${_rel}' не найден")
        endif()
        get_property(_claimed GLOBAL PROPERTY TRUST_COMPONENT_SOURCES)
        if(_rel IN_LIST _claimed)
            message(FATAL_ERROR "trust_claim_sources: источник '${_rel}' уже зарегистрирован")
        endif()
        set_property(GLOBAL APPEND PROPERTY TRUST_COMPONENT_SOURCES "${_rel}")
    endforeach()
endfunction()

# trust_component_verify_sources()
# Каждый src/<component>/**.cpp зарегистрирован ровно в одном trust_component(SOURCES ...).
# Корневые TU (src/*.cpp) принадлежат исполняемым целям и здесь не проверяются.
function(trust_component_verify_sources)
    file(GLOB_RECURSE _all_rel RELATIVE "${TRUST_SRC_DIR}" "${TRUST_SRC_DIR}/*.cpp")
    get_property(_claimed GLOBAL PROPERTY TRUST_COMPONENT_SOURCES)

    set(_unclaimed "")
    foreach(_rel IN LISTS _all_rel)
        # Компонентные TU лежат на глубине >= 2 (src/<component>/...).
        if(_rel MATCHES "^[^/]+/.+")
            if(NOT _rel IN_LIST _claimed)
                list(APPEND _unclaimed "${_rel}")
            endif()
        endif()
    endforeach()

    if(_unclaimed)
        string(REPLACE ";" "\n  " _msg "${_unclaimed}")
        message(FATAL_ERROR
            "trust_component_verify_sources: TU не зарегистрированы ни в одной цели компонента:\n  ${_msg}")
    endif()
    message(STATUS "trust_component_verify_sources: OK (${_claimed})")
endfunction()

