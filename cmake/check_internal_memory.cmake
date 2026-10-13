# cmake/check_internal_memory.cmake
# CI-хранитель системы персистентной памяти (MEMORY.md).
#
# Правила:
#   A) MEMORY.md в корне компонента описывает только публичный интерфейс и общие правила;
#      детали реализации — в MEMORY.md internal-подкаталогов (в script-режиме проверяется
#      структура, а не семантика содержимого);
#   B) MEMORY.md ОПЦИОНАЛЕН для малой реализации: не более 3 файлов (*.hpp/*.h/*.cpp)
#      суммарным размером не более 16 KiB — тогда инварианты живут в WHY-комментариях кода;
#   C) существующий MEMORY.md обязан иметь шапку (scope/role/last_reviewed/review_period/max_size),
#      раздел «## Architecture», размер не меньше 1000 байт (нижний порог полезности) и не больше max_size;
#   D) корневой MEMORY.md компонента ссылается на каждый internal-подкаталог;
#   E) подкаталог не может быть одновременно публичным и внутренним.
#
# Использование: cmake -DROOT=<корень> -P cmake/check_internal_memory.cmake

include("${CMAKE_CURRENT_LIST_DIR}/guard_common.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/component_manifest.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/internal_dirs.cmake")

guard_begin(internal_memory_guard)

set(_memory_min_bytes 1000)
set(_small_max_files 3)
set(_small_max_bytes 16384)

guard_components(_components)
if(NOT _components)
    message(FATAL_ERROR "internal_memory_guard: не найдено ни одного компонента в ${ROOT}/include")
endif()

# _memory_optional_for(<out_optional> <out_files> <out_bytes> <dir>...)
# MEMORY.md опционален для малой реализации (*.hpp/*.h/*.cpp); счётчики — для диагностики.
# Подсчёт файлов/байт — общий с subdir_layout_guard (cmake/internal_dirs.cmake).
function(_memory_optional_for out_optional out_files out_bytes)
    trust_subdir_impl_size(_count _total ${ARGN})
    set(_optional FALSE)
    if(_count LESS_EQUAL ${_small_max_files} AND _total LESS_EQUAL ${_small_max_bytes})
        set(_optional TRUE)
    endif()
    set(${out_optional} "${_optional}" PARENT_SCOPE)
    set(${out_files} "${_count}" PARENT_SCOPE)
    set(${out_bytes} "${_total}" PARENT_SCOPE)
endfunction()

macro(_check_memory_meta _file _label)
    if(NOT EXISTS "${_file}")
        if(NOT _memory_optional)
            guard_violation("${_label}: отсутствует MEMORY.md, хотя реализация не мала "
                            "(${_impl_files} файлов, ${_impl_bytes} байт > порога "
                            "${_small_max_files} файлов / ${_small_max_bytes} байт)")
        endif()
    else()
        file(READ "${_file}" _mem)
        foreach(_key IN ITEMS "scope:" "role:" "last_reviewed:" "review_period:" "max_size:")
            if(NOT _mem MATCHES "> ${_key}")
                guard_violation("${_label}: в MEMORY.md нет обязательного поля шапки '${_key}'")
            endif()
        endforeach()
        if(NOT _mem MATCHES "## Architecture")
            guard_violation("${_label}: в MEMORY.md нет обязательного раздела '## Architecture'")
        endif()
        string(REGEX MATCH "> max_size: *([0-9]+)" _ms "${_mem}")
        set(_max "${CMAKE_MATCH_1}")
        file(SIZE "${_file}" _size)
        if(_size LESS ${_memory_min_bytes})
            guard_violation("${_label}: MEMORY.md меньше нижнего порога ${_memory_min_bytes} байт "
                            "(${_size}) — перенесите знания в WHY-комментарии кода/README.md и удалите файл")
        endif()
        if(_max AND _size GREATER _max)
            guard_violation("${_label}: MEMORY.md превышает max_size (${_size} > ${_max}) — требуется ревизия/разделение")
        endif()
    endif()
endmacro()

foreach(_component IN LISTS _components)
    set(_dir "${ROOT}/include/${_component}")
    set(_src "${ROOT}/src/${_component}")

    trust_manifest_parse("${_dir}/CMakeLists.txt" M)

    # Корневой MEMORY.md: реализация компонента целиком (включая подкаталоги).
    _memory_optional_for(_memory_optional _impl_files _impl_bytes "${_dir}" "${_src}")
    _check_memory_meta("${_dir}/MEMORY.md" "include/${_component}")

    # Корневой MEMORY.md обязан ссылаться на каждый объявленный подкаталог (таблица каталогов):
    # детали живут в памяти своего подкаталога, корень только указывает на них.
    if(EXISTS "${_dir}/MEMORY.md")
        file(READ "${_dir}/MEMORY.md" _root_mem)
        foreach(_sub IN LISTS M_INTERNAL_DIRS M_PUBLIC_DIRS)
            if(NOT _root_mem MATCHES "${_sub}/")
                guard_violation("include/${_component}/MEMORY.md: нет ссылки на подкаталог '${_sub}/' "
                                "(корень описывает только публичный интерфейс и таблицу каталогов)")
            endif()
        endforeach()
    endif()

    foreach(_sub IN LISTS M_INTERNAL_DIRS)
        if(_sub IN_LIST M_PUBLIC_DIRS)
            guard_violation("include/${_component}/${_sub}/: объявлен одновременно в INTERNAL_DIRS и PUBLIC_DIRS")
        endif()
    endforeach()

    # Каждый подкаталог (рекурсивно, в include/ и src/) — самостоятельная ответственность:
    # своя память либо WHY-комментарии, если реализация мала.
    set(_subdirs "")
    foreach(_root IN ITEMS "${_dir}" "${_src}")
        if(NOT IS_DIRECTORY "${_root}")
            continue()
        endif()
        file(GLOB_RECURSE _found LIST_DIRECTORIES true "${_root}/*")
        foreach(_sub IN LISTS _found)
            if(NOT IS_DIRECTORY "${_sub}")
                continue()
            endif()
            file(RELATIVE_PATH _rel "${_root}" "${_sub}")
            list(APPEND _subdirs "${_rel}")
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _subdirs)
    list(SORT _subdirs)

    foreach(_rel IN LISTS _subdirs)
        if(IS_DIRECTORY "${_dir}/${_rel}")
            _memory_optional_for(_memory_optional _impl_files _impl_bytes "${_dir}/${_rel}" "${_src}/${_rel}")
            _check_memory_meta("${_dir}/${_rel}/MEMORY.md" "include/${_component}/${_rel}")
        else()
            _memory_optional_for(_memory_optional _impl_files _impl_bytes "${_src}/${_rel}")
            _check_memory_meta("${_src}/${_rel}/MEMORY.md" "src/${_component}/${_rel}")
        endif()
    endforeach()
endforeach()

guard_end("MEMORY.md: опционален для малой реализации; существующий — корректен и в пределах 1000..max_size байт")
