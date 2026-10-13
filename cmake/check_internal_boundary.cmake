# cmake/check_internal_boundary.cmake
# CI-хранитель границы «публичный API компонента / детали реализации».
#
# Правило: код одного компонента (include/<A>/**, src/<A>/**) не может включать заголовок
# internal-подкаталога другого компонента: #include "<B>/<dir>/…". Термин: internal-подкаталог
# (аналог `internal/` в Go) — каталог деталей реализации, объявленный в манифесте
# trust_component(INTERNAL_DIRS ...). Публичный API другого компонента — только заголовки
# в корне include/<B>/ (манифест PUBLIC_HEADERS).
#
# Исключение: намеренно публикуемые вложенные каталоги (PUBLIC_DIRS манифеста, напр.
# runtime/trust — ABI-заголовки пакета).
#
# Тесты (test/**) — внутренние потребители, им разрешено включать internal-подкаталоги.
#
# Использование: cmake -DROOT=<корень> -P cmake/check_internal_boundary.cmake

include("${CMAKE_CURRENT_LIST_DIR}/guard_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/component_manifest.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/internal_dirs.cmake")

guard_begin(internal_boundary_guard)

guard_components(_components)
if(NOT _components)
    message(FATAL_ERROR "internal_boundary_guard: не найдено ни одного компонента в ${ROOT}/include")
endif()

# Объявленные подкаталоги каждого компонента — из манифестов (единый источник истины).
# Классификация include — по самому длинному объявленному префиксу (cmake/internal_dirs.cmake).
foreach(_component IN LISTS _components)
    trust_manifest_parse("${ROOT}/include/${_component}/CMakeLists.txt" M)
    set(_manifest_public_dirs_${_component} "${M_PUBLIC_DIRS}")
    set(_manifest_internal_dirs_${_component} "${M_INTERNAL_DIRS}")
endforeach()

foreach(_component IN LISTS _components)
    foreach(_area IN ITEMS include src)
        set(_dir "${ROOT}/${_area}/${_component}")
        if(NOT IS_DIRECTORY "${_dir}")
            continue()
        endif()
        guard_include_edges(_edges "${_dir}")
        foreach(_edge IN LISTS _edges)
            string(REGEX REPLACE "\\|.*$" "" _rel "${_edge}")
            string(REGEX REPLACE "^.*\\|" "" _inc "${_edge}")
            if(_inc STREQUAL "")
                continue()
            endif()
            string(REPLACE "/" ";" _segments "${_inc}")
            list(LENGTH _segments _count)
            if(_count LESS 2)
                continue()
            endif()
            list(GET _segments 0 _inc_root)
            if(_inc_root STREQUAL _component)
                continue()
            endif()
            list(FIND _components "${_inc_root}" _is_component)
            if(_is_component EQUAL -1)
                continue()  # системный/внешний заголовок
            endif()
            # Путь внутри чужого компонента: "<c>/<file>" (корень — публично) или "<c>/<sub>/...".
            string(REGEX REPLACE "^${_inc_root}/" "" _inc_rel "${_inc}")
            trust_subdir_classify(_kind
                "${_manifest_public_dirs_${_inc_root}}"
                "${_manifest_internal_dirs_${_inc_root}}"
                "${_inc_rel}")
            if(_kind STREQUAL "internal")
                guard_violation("${_rel}: include internal-подкаталога '${_inc}' другого компонента ('${_component}' -> '${_inc_root}')")
            endif()
        endforeach()
    endforeach()
endforeach()

guard_end("включать internal-подкаталоги другого компонента запрещено — используйте его публичный API")
