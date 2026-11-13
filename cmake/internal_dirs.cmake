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
# Классификация — только по объявленным подкаталогам: размеры файлов и число TU не являются
# критерием раскладки (AGENTS.md: формальные метрики не заменяют смысловое решение).

include_guard(GLOBAL)

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
