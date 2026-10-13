# cmake/internal_dirs.cmake
# Единственная реализация классификации подкаталогов компонента.
#
# Подкаталог компонента относится к одному из двух видов:
#   PUBLIC_DIRS   — публичный подкаталог (намеренно публикуемые заголовки, напр. runtime/trust);
#   INTERNAL_DIRS — internal-подкаталог (детали реализации, снаружи включать запрещено).
# Объявленный подкаталог РЕКУРСИВЕН: любой путь глубже него принадлежит ему, пока не объявлен
# отдельный подкаталог. Классификация — по САМОМУ ДЛИННОМУ объявленному префиксу.
#
# Модуль используется и в configure-режиме (component.cmake), и в script-режиме
# (`cmake -P cmake/check_*.cmake`), поэтому данные передаются списками, а не читаются из целей.
#
# Порог «малого компонента» определён здесь ОДИН раз: маленькой реализации не нужны ни
# internal-подкаталоги, ни обязательный MEMORY.md (инварианты живут в WHY-комментариях кода).

include_guard(GLOBAL)

set(TRUST_SMALL_MAX_IMPL_FILES 4)
set(TRUST_SMALL_MAX_IMPL_BYTES 32768)
set(TRUST_SMALL_MAX_PUBLIC_HEADERS 4)

# trust_subdir_classify(<out_var> <public_dirs> <internal_dirs> <rel_path>)
# off_var -> "public" | "internal" | "unknown".
function(trust_subdir_classify out_var public_dirs internal_dirs rel_path)
    set(_kind "unknown")
    set(_best_len -1)
    foreach(_kind_name IN ITEMS public internal)
        set(_dirs "${public_dirs}")
        if(_kind_name STREQUAL "internal")
            set(_dirs "${internal_dirs}")
        endif()
        foreach(_dir IN LISTS _dirs)
            if(rel_path STREQUAL "${_dir}" OR rel_path MATCHES "^${_dir}/")
                string(LENGTH "${_dir}" _len)
                if(_len GREATER _best_len)
                    set(_best_len ${_len})
                    set(_kind "${_kind_name}")
                endif()
            endif()
        endforeach()
    endforeach()
    set(${out_var} "${_kind}" PARENT_SCOPE)
endfunction()

# trust_subdir_impl_size(<out_files> <out_bytes> <dir>...)
# Количество и суммарный размер файлов реализации (*.hpp/*.h/*.cpp) рекурсивно по каталогам.
# Единственная реализация подсчёта: используется и гейтом subdir_layout, и гейтом internal_memory.
function(trust_subdir_impl_size out_files out_bytes)
    set(_files "")
    foreach(_dir IN LISTS ARGN)
        if(IS_DIRECTORY "${_dir}")
            file(GLOB_RECURSE _hits "${_dir}/*.hpp" "${_dir}/*.h" "${_dir}/*.cpp")
            list(APPEND _files ${_hits})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _files)
    list(LENGTH _files _count)
    set(_total 0)
    foreach(_file IN LISTS _files)
        file(SIZE "${_file}" _file_size)
        math(EXPR _total "${_total} + ${_file_size}")
    endforeach()
    set(${out_files} "${_count}" PARENT_SCOPE)
    set(${out_bytes} "${_total}" PARENT_SCOPE)
endfunction()

# trust_is_small_component(<out_var> <include_dir> <src_dir>)
# Малая реализация: ≤TRUST_SMALL_MAX_IMPL_FILES файлов реализации, ≤TRUST_SMALL_MAX_IMPL_BYTES
# байт и ≤TRUST_SMALL_MAX_PUBLIC_HEADERS заголовков в корне компонента.
function(trust_is_small_component out_var include_dir src_dir)
    trust_subdir_impl_size(_impl_files _impl_bytes "${src_dir}")
    file(GLOB _root_headers "${include_dir}/*.hpp" "${include_dir}/*.h")
    list(LENGTH _root_headers _header_count)
    set(_small FALSE)
    if(_impl_files LESS_EQUAL ${TRUST_SMALL_MAX_IMPL_FILES}
       AND _impl_bytes LESS_EQUAL ${TRUST_SMALL_MAX_IMPL_BYTES}
       AND _header_count LESS_EQUAL ${TRUST_SMALL_MAX_PUBLIC_HEADERS})
        set(_small TRUE)
    endif()
    set(${out_var} "${_small}" PARENT_SCOPE)
endfunction()
