# cmake/guard_common.cmake
# Общая обвязка CI-гейтов (cmake/check_*.cmake): проверка ROOT, сбор файлов, накопление
# нарушений и единый отчёт с кодом возврата. Доменные правила и allowlist остаются в
# конкретном гейте — здесь только инфраструктура.
#
# ВАЖНО: guard_begin/guard_violation/guard_end — МАКРОСЫ, а не функции: CMake создаёт область
# видимости в function(), поэтому накопленный список нарушений не «протёк» бы в вызывающий скрипт.
# guard_files — функция (возвращает значение через PARENT_SCOPE).
#
# Использование: cmake -DROOT=<корень> -P cmake/check_<rule>.cmake (guard_begin → guard_violation → guard_end).

macro(guard_begin guard_name)
    if(NOT ROOT)
        message(FATAL_ERROR "${guard_name}: ROOT is required")
    endif()
    set(_guard_name "${guard_name}")
    set(_guard_violations "")
endmacro()

macro(guard_violation msg)
    list(APPEND _guard_violations "${msg}")
endmacro()

# guard_components(<out_var>)
# Единственное место перечисления компонентов проекта: include/<component>/CMakeLists.txt.
function(guard_components out_var)
    file(GLOB _cmakes "${ROOT}/include/*/CMakeLists.txt")
    set(_names "")
    foreach(_cmake IN LISTS _cmakes)
        get_filename_component(_dir "${_cmake}" DIRECTORY)
        get_filename_component(_name "${_dir}" NAME)
        list(APPEND _names "${_name}")
    endforeach()
    list(REMOVE_DUPLICATES _names)
    list(SORT _names)
    set(${out_var} "${_names}" PARENT_SCOPE)
endfunction()

# guard_include_edges(<out_var> <root>...)
# Единственная реализация разбора include-графа: рёбра "<файл относительно ROOT>|<заголовок>".
function(guard_include_edges out_var)
    set(_edges "")
    foreach(_root IN LISTS ARGN)
        guard_files(_files ROOTS "${_root}" GLOBS "*.hpp" "*.h" "*.cpp" "*.cppm")
        foreach(_file IN LISTS _files)
            file(RELATIVE_PATH _rel "${ROOT}" "${_file}")
            file(STRINGS "${_file}" _lines REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
            foreach(_line IN LISTS _lines)
                string(REGEX MATCH "\"([^\"]+)\"" _match "${_line}")
                if(_match)
                    list(APPEND _edges "${_rel}|${CMAKE_MATCH_1}")
                endif()
            endforeach()
        endforeach()
    endforeach()
    set(${out_var} "${_edges}" PARENT_SCOPE)
endfunction()

# guard_files(<out_var> ROOTS <dir>... GLOBS <pattern>...)
# Рекурсивный сбор файлов по наборам корней и шаблонов (без дублей, порядок стабилен).
function(guard_files out_var)
    cmake_parse_arguments(GF "" "" "ROOTS;GLOBS" ${ARGN})
    set(_found "")
    foreach(_root IN LISTS GF_ROOTS)
        foreach(_glob IN LISTS GF_GLOBS)
            file(GLOB_RECURSE _hits "${_root}/${_glob}")
            list(APPEND _found ${_hits})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _found)
    list(SORT _found)
    set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

macro(guard_end summary)
    if(_guard_violations)
        foreach(_violation IN LISTS _guard_violations)
            message(SEND_ERROR "${_guard_name}: ${_violation}")
        endforeach()
        message(FATAL_ERROR "${_guard_name}: ${summary}")
    endif()
    message(STATUS "${_guard_name}: OK (${ROOT})")
endmacro()
