# cmake/parallelism.cmake
# Единственная реализация политики параллелизма сборки: бюджет памяти -> число задач.
#
# Политика (шапка корневого CMakeLists.txt, AGENTS.md п.14):
#   budget = min(MemAvailable, запас внутри cgroup-лимита, TOTAL_PHYSICAL_MEMORY)
#   jobs   = clamp(floor((budget - RESERVE) / PER_JOB), 1, NUMBER_OF_LOGICAL_CORES)
#
# Почему так:
#   - бюджет берётся из ДОСТУПНОЙ памяти (MemAvailable), а не из общей: занятая IDE/ОС
#     память не должна попадать в бюджет компиляции (на 16 GiB рабочей станции занято ~7 GiB);
#   - cgroup: учитывается ЗАПАС (limit - current), а не только лимит: несколько сессий
#     в одном cgroup делят его, и каждая иначе получила бы полный бюджет -> OOM;
#     `memory.high` (soft limit) действует так же, как жёсткий лимит, если он меньше;
#   - пики clang++ на этом проекте (RSS, -fsyntax-only): типовой TU 0.24..0.35 GiB,
#     тяжёлый TU c libtorch (src/runtime/tensor.cpp — единственный в проекте) 0.92 GiB;
#     полная кодогенерация (-c, Debug) добавляет ~1.3x -> типовой ~0.5 GiB, torch ~1.2 GiB;
#     1100 MiB/задача — запас поверх типового пика, torch-пик укладывается;
#     16 GiB RAM / MemAvailable ~9 GiB -> -j8..9 (проверено на рабочей станции, было -j3);
#   - RESERVE — страховка ТОЛЬКО когда MemAvailable недоступен (источник budget=total);
#   - нечитаемый/неожиданный cgroup-файл — это «нет данных», а не ошибка конфигурации:
#     в контейнерах без memory-контроллера файлов просто нет.
#
# Скрипт-режим `cmake/build_jobs.cmake` переиспользует этот файл (cmake -P), поэтому
# число пересчитывается на каждой сборке: состояние памяти на момент configure и на момент
# сборки различается.

include_guard(GLOBAL)

# Defaults are plain variables (not CACHE) so that a policy change applies on the next
# configure; an explicit -D...=N still wins because it defines the variable.
if(NOT DEFINED TRUST_BUILD_MEMORY_RESERVE_MIB)
    set(TRUST_BUILD_MEMORY_RESERVE_MIB 3000)
endif()
if(NOT DEFINED TRUST_BUILD_MEMORY_PER_JOB_MIB)
    set(TRUST_BUILD_MEMORY_PER_JOB_MIB 1100)
endif()
if(NOT DEFINED TRUST_BUILD_JOBS)
    set(TRUST_BUILD_JOBS "")
endif()

# _trust_read_meminfo_mib(<key> <out_mib>): 0 — значение недоступно (не Linux/нет файла).
function(_trust_read_meminfo_mib _key _out)
    set(${_out} 0 PARENT_SCOPE)
    if(NOT EXISTS "/proc/meminfo")
        return()
    endif()
    file(STRINGS "/proc/meminfo" _lines REGEX "^${_key}:")
    if(NOT _lines)
        return()
    endif()
    list(GET _lines 0 _line)
    string(REGEX MATCH "[0-9]+" _kb "${_line}")
    if(NOT _kb MATCHES "^[0-9]+$")
        message(WARNING "parallelism: unexpected '${_key}' line in /proc/meminfo: '${_line}'")
        return()
    endif()
    math(EXPR _mib "(${_kb} + 1023) / 1024")
    set(${_out} ${_mib} PARENT_SCOPE)
endfunction()

# _trust_read_cgroup_value_mib(<file> <out_mib>): 0 — нет файла / нет значения / "max".
function(_trust_read_cgroup_value_mib _file _out)
    set(${_out} 0 PARENT_SCOPE)
    if(NOT EXISTS "${_file}")
        return()
    endif()
    file(READ "${_file}" _raw)
    string(STRIP "${_raw}" _raw)
    if(_raw STREQUAL "max" OR _raw STREQUAL "")
        return()
    endif()
    if(NOT _raw MATCHES "^[0-9]+$")
        message(WARNING "parallelism: unexpected content '${_raw}' in ${_file} — считаю «нет данных»")
        return()
    endif()
    math(EXPR _mib "${_raw} / 1048576")
    # cgroup v1 "unlimited" is a sentinel near 2^63 bytes; anything > 1 TiB is not a limit.
    if(_mib GREATER 1048576)
        set(_mib 0)
    endif()
    set(${_out} ${_mib} PARENT_SCOPE)
endfunction()

# _trust_cgroup_chain(<out_root> <out_limit> <out_used> <out_high> <out_path>)
# Пустая <out_root> — cgroup-контроллер памяти недоступен. Best effort: для v1 путь берётся
# относительно memory-монта; вложенные v1-иерархии через mountinfo не разрешаются.
function(_trust_cgroup_chain _out_root _out_limit _out_used _out_high _out_path)
    set(${_out_root} "" PARENT_SCOPE)
    set(${_out_limit} "" PARENT_SCOPE)
    set(${_out_used} "" PARENT_SCOPE)
    set(${_out_high} "" PARENT_SCOPE)
    set(${_out_path} "" PARENT_SCOPE)
    if(NOT EXISTS "/proc/self/cgroup")
        return()
    endif()

    file(STRINGS "/proc/self/cgroup" _cg_lines)
    foreach(_line IN LISTS _cg_lines)
        if(_line MATCHES "^0::(.*)$")
            set(${_out_root} "/sys/fs/cgroup" PARENT_SCOPE)
            set(${_out_limit} "memory.max" PARENT_SCOPE)
            set(${_out_used} "memory.current" PARENT_SCOPE)
            set(${_out_high} "memory.high" PARENT_SCOPE)
            set(${_out_path} "${CMAKE_MATCH_1}" PARENT_SCOPE)
            return()
        elseif(_line MATCHES "^[0-9]+:memory:(.*)$")
            set(${_out_root} "/sys/fs/cgroup/memory" PARENT_SCOPE)
            set(${_out_limit} "memory.limit_in_bytes" PARENT_SCOPE)
            set(${_out_used} "memory.usage_in_bytes" PARENT_SCOPE)
            set(${_out_path} "${CMAKE_MATCH_1}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

# _trust_cgroup_limit_mib(<out_mib>): минимальный лимит по цепочке от текущей cgroup до
# корня (лимиты предков тоже действуют). 0 — лимита нет.
function(_trust_cgroup_limit_mib _out)
    set(${_out} 0 PARENT_SCOPE)
    _trust_cgroup_chain(_root _limit_name _used_name _high_name _path)
    if(_root STREQUAL "")
        return()
    endif()

    set(_limit 0)
    while(TRUE)
        _trust_read_cgroup_value_mib("${_root}${_path}/${_limit_name}" _file_limit)
        if(_file_limit GREATER 0 AND (_limit EQUAL 0 OR _file_limit LESS _limit))
            set(_limit ${_file_limit})
        endif()
        if(NOT _high_name STREQUAL "")
            _trust_read_cgroup_value_mib("${_root}${_path}/${_high_name}" _file_high)
            if(_file_high GREATER 0 AND (_limit EQUAL 0 OR _file_high LESS _limit))
                set(_limit ${_file_high})
            endif()
        endif()
        if(_path STREQUAL "" OR _path STREQUAL "/")
            break()
        endif()
        get_filename_component(_path "${_path}" DIRECTORY)
        if(_path STREQUAL "/")
            set(_path "")
        endif()
    endwhile()

    set(${_out} ${_limit} PARENT_SCOPE)
endfunction()

# _trust_cgroup_used_mib(<out_mib>): текущее потребление cgroup (0 — нет данных).
function(_trust_cgroup_used_mib _out)
    set(${_out} 0 PARENT_SCOPE)
    _trust_cgroup_chain(_root _limit_name _used_name _high_name _path)
    if(_root STREQUAL "" OR _used_name STREQUAL "")
        return()
    endif()
    _trust_read_cgroup_value_mib("${_root}${_path}/${_used_name}" _used)
    set(${_out} ${_used} PARENT_SCOPE)
endfunction()

# trust_compute_build_jobs(<out_jobs>)
# В caller scope также выставляет TRUST_BUILD_MEMORY_BUDGET_MIB, TRUST_BUILD_CORES и
# TRUST_BUILD_MEMORY_SOURCE (откуда взят бюджет: MemAvailable | cgroup | total).
function(trust_compute_build_jobs _out_jobs)
    if(NOT TRUST_BUILD_MEMORY_PER_JOB_MIB MATCHES "^[0-9]+$" OR TRUST_BUILD_MEMORY_PER_JOB_MIB LESS 1)
        message(FATAL_ERROR
            "TRUST_BUILD_MEMORY_PER_JOB_MIB must be a positive integer (got '${TRUST_BUILD_MEMORY_PER_JOB_MIB}')")
    endif()
    if(NOT TRUST_BUILD_MEMORY_RESERVE_MIB MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "TRUST_BUILD_MEMORY_RESERVE_MIB must be a non-negative integer (got '${TRUST_BUILD_MEMORY_RESERVE_MIB}')")
    endif()

    cmake_host_system_information(RESULT _cores QUERY NUMBER_OF_LOGICAL_CORES)
    cmake_host_system_information(RESULT _mem_total QUERY TOTAL_PHYSICAL_MEMORY)
    if(NOT _cores MATCHES "^[0-9]+$" OR NOT _mem_total MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "parallelism: cannot determine host cores/memory (cores='${_cores}', memory='${_mem_total}')")
    endif()

    set(_budget ${_mem_total})
    set(_source "total - reserve")
    set(_subtract_reserve TRUE)

    _trust_read_meminfo_mib("MemAvailable" _mem_avail)
    if(_mem_avail GREATER 0 AND _mem_avail LESS _budget)
        # MemAvailable уже исключает занятую память: резерв повторно НЕ вычитается.
        set(_budget ${_mem_avail})
        set(_source "MemAvailable")
        set(_subtract_reserve FALSE)
    endif()

    _trust_cgroup_limit_mib(_cg_limit)
    _trust_cgroup_used_mib(_cg_used)
    if(_cg_limit GREATER 0)
        if(_cg_used GREATER 0 AND _cg_used LESS _cg_limit)
            math(EXPR _cg_avail "${_cg_limit} - ${_cg_used}")
            if(_cg_avail LESS _budget)
                set(_budget ${_cg_avail})
                set(_source "cgroup headroom (${_cg_avail} MiB of ${_cg_limit} MiB)")
                set(_subtract_reserve FALSE)
            endif()
        elseif(_cg_limit LESS _budget)
            set(_budget ${_cg_limit})
            set(_source "cgroup limit")
            set(_subtract_reserve FALSE)
        endif()
    endif()

    set(TRUST_BUILD_CORES "${_cores}" PARENT_SCOPE)
    set(TRUST_BUILD_MEMORY_BUDGET_MIB "${_budget}" PARENT_SCOPE)
    set(TRUST_BUILD_MEMORY_SOURCE "${_source}" PARENT_SCOPE)

    if(NOT TRUST_BUILD_JOBS STREQUAL "" AND NOT TRUST_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "TRUST_BUILD_JOBS must be a positive integer (got '${TRUST_BUILD_JOBS}')")
    endif()
    if(TRUST_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
        set(${_out_jobs} "${TRUST_BUILD_JOBS}" PARENT_SCOPE)
        return()
    endif()

    set(_jobs 1)
    if(_subtract_reserve)
        if(_budget GREATER TRUST_BUILD_MEMORY_RESERVE_MIB)
            math(EXPR _jobs "(${_budget} - ${TRUST_BUILD_MEMORY_RESERVE_MIB}) / ${TRUST_BUILD_MEMORY_PER_JOB_MIB}")
        endif()
    else()
        math(EXPR _jobs "${_budget} / ${TRUST_BUILD_MEMORY_PER_JOB_MIB}")
    endif()
    if(_jobs LESS 1)
        set(_jobs 1)
    endif()
    if(_jobs GREATER _cores)
        set(_jobs ${_cores})
    endif()

    if(_jobs EQUAL 1)
        message(STATUS "parallelism: -j1 (budget ${_budget} MiB [${_source}], per-job ${TRUST_BUILD_MEMORY_PER_JOB_MIB} MiB, cores ${_cores})")
    endif()

    set(${_out_jobs} "${_jobs}" PARENT_SCOPE)
endfunction()

# -- Тестовая политика параллелизма (ctest -j): НЕ по памяти компилятора --
# Сборка калибрована по RSS clang (parallelism.cmake выше) и к тестам неприменима:
# lit уже использует 16 воркеров + unit-шарды/примеры требуют просто число ядер.
# Дефолт — ядра; TRUST_TEST_JOBS (cache, число) переопределяет.
function(trust_compute_test_jobs _out_jobs)
    cmake_host_system_information(RESULT _cores QUERY NUMBER_OF_LOGICAL_CORES)
    if(NOT _cores MATCHES "^[1-9][0-9]*$")
        set(_cores 4)
    endif()
    if(TRUST_TEST_JOBS MATCHES "^[1-9][0-9]*$")
        set(_cores ${TRUST_TEST_JOBS})
    endif()
    set(${_out_jobs} "${_cores}" PARENT_SCOPE)
endfunction()
