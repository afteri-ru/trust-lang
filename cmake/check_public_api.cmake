# cmake/check_public_api.cmake
# CI-хранитель публичного API компонента.
#
# Правила:
#   A) каждый заголовок в КОРНЕ include/<component>/ объявлен в манифесте trust_component(PUBLIC_HEADERS),
#      либо находится в internal-подкаталоге (тогда он не публичный по определению);
#   B) каждый объявленный PUBLIC_HEADERS заголовок реально доступен снаружи компонента:
#      его включает файл другого компонента (include/**, src/**) ЛИБО он включается другим
#      объявленным заголовком-«зонтиком» (umbrella header) того же компонента — иначе это мёртвый API;
#   C) каждый подкаталог include/<component>/ объявлен в INTERNAL_DIRS (детали реализации) или
#      PUBLIC_DIRS (намеренно публикуемый вложенный каталог, напр. runtime/trust).
#
# Использование: cmake -DROOT=<корень> -P cmake/check_public_api.cmake

include("${CMAKE_CURRENT_LIST_DIR}/guard_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/component_manifest.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/internal_dirs.cmake")

guard_begin(public_api_guard)

guard_components(_components)
if(NOT _components)
    message(FATAL_ERROR "public_api_guard: не найдено ни одного компонента в ${ROOT}/include")
endif()

# -- Include-граф: "<файл>|<включаемый заголовок>" (единая реализация — guard_common) ----
guard_include_edges(_edges "${ROOT}/include" "${ROOT}/src")

# Каждый объявленный заголовок обязан быть доступен снаружи (напрямую или через зонтик).

foreach(_component IN LISTS _components)
    set(_dir "${ROOT}/include/${_component}")
    set(_cmake "${_dir}/CMakeLists.txt")

    trust_manifest_parse("${_cmake}" M)
    if(NOT M_NAME STREQUAL _component)
        guard_violation("include/${_component}/CMakeLists.txt: trust_component(${M_NAME}) не совпадает с каталогом")
    endif()

    # -- A) все заголовки корня объявлены ---------------------------------
    file(GLOB _root_headers "${_dir}/*.h" "${_dir}/*.hpp")
    foreach(_hdr_path IN LISTS _root_headers)
        get_filename_component(_hdr "${_hdr_path}" NAME)
        if(NOT _hdr IN_LIST M_PUBLIC_HEADERS)
            guard_violation("include/${_component}/${_hdr}: заголовок корня не объявлен в PUBLIC_HEADERS "
                            "(публичный API объявляется явно, детали — в INTERNAL_DIRS)")
        endif()
    endforeach()
    foreach(_hdr IN LISTS M_PUBLIC_HEADERS)
        if(NOT EXISTS "${_dir}/${_hdr}")
            guard_violation("include/${_component}/${_hdr}: объявлен в PUBLIC_HEADERS, но файла нет в корне компонента")
        endif()
    endforeach()

    # -- C) все подкаталоги классифицированы (рекурсивно) -------------------
    # Объявленный подкаталог рекурсивен: вложенный каталог внутри него классифицируется по
    # самому длинному объявленному префиксу, поэтому отдельного объявления не требует.
    file(GLOB_RECURSE _subdirs LIST_DIRECTORIES true "${_dir}/*")
    foreach(_sub IN LISTS _subdirs)
        if(NOT IS_DIRECTORY "${_sub}")
            continue()
        endif()
        file(RELATIVE_PATH _subrel "${_dir}" "${_sub}")
        trust_subdir_classify(_sub_kind "${M_PUBLIC_DIRS}" "${M_INTERNAL_DIRS}" "${_subrel}")
        if(_sub_kind STREQUAL "unknown")
            guard_violation("include/${_component}/${_subrel}/: подкаталог не объявлен ни в INTERNAL_DIRS, ни в PUBLIC_DIRS")
        endif()
    endforeach()

    # -- B) объявленный заголовок доступен снаружи -------------------------
    set(_external_targets "")
    set(_internal_edges "")
    foreach(_edge IN LISTS _edges)
        string(REGEX REPLACE "\\|.*$" "" _file "${_edge}")
        string(REGEX REPLACE "^.*\\|" "" _target "${_edge}")
        if(_file MATCHES "^include/${_component}/" OR _file MATCHES "^src/${_component}/")
            list(APPEND _internal_edges "${_file}|${_target}")
        else()
            list(APPEND _external_targets "${_target}")
        endif()
    endforeach()

    set(_used "")
    foreach(_hdr IN LISTS M_PUBLIC_HEADERS)
        if("${_component}/${_hdr}" IN_LIST _external_targets)
            list(APPEND _used "${_hdr}")
        endif()
    endforeach()

    # Зонтики: объявленный и уже используемый заголовок может включать другой объявленный.
    set(_changed TRUE)
    while(_changed)
        set(_changed FALSE)
        foreach(_hdr IN LISTS M_PUBLIC_HEADERS)
            if(_hdr IN_LIST _used)
                continue()
            endif()
            foreach(_user IN LISTS _used)
                list(FIND _internal_edges "include/${_component}/${_user}|${_component}/${_hdr}" _found)
                if(NOT _found EQUAL -1)
                    list(APPEND _used "${_hdr}")
                    set(_changed TRUE)
                    break()
                endif()
            endforeach()
        endforeach()
    endwhile()

    foreach(_hdr IN LISTS M_PUBLIC_HEADERS)
        if(NOT _hdr IN_LIST _used)
            guard_violation("include/${_component}/${_hdr}: объявлен публичным, но его не включает ни один "
                            "потребитель и ни один зонтичный заголовок компонента (мёртвый API)")
        endif()
    endforeach()
endforeach()

guard_end("публичный API компонентов должен быть объявлен, доступен снаружи и полон; подкаталоги — классифицированы")
