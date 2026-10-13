# cmake/test.cmake
# Регистрация CTest-суитов: обязательный TIMEOUT, рабочий каталог и фикстуры порядка задаются
# в одном месте.
#
# trust_add_test_suite(<name>
#     TIMEOUT <sec>                       # обязателен: тесты без лимита запрещены
#     [WORKING_DIRECTORY <dir>]
#     [FIXTURES_REQUIRED <fixtures>] [FIXTURES_SETUP <fixtures>]
#     [LABELS <label...>]
#     [RUN_SERIAL]
#     COMMAND <cmd...>)                   # COMMAND идёт последним
#
# TRUST_SUITE_FIXTURES — фикстуры, которые поднимает каждая ранняя (unit/lit/vscode) suite,
# чтобы поздние стадии (examples/integration/package) шли только после них.

include_guard(GLOBAL)

set(TRUST_SUITE_FIXTURES "examples_deps;package_deps")

function(trust_add_test_suite _name)
    cmake_parse_arguments(T "RUN_SERIAL"
        "TIMEOUT;WORKING_DIRECTORY;FIXTURES_REQUIRED;FIXTURES_SETUP"
        "COMMAND;LABELS" ${ARGN})

    if(NOT T_COMMAND)
        message(FATAL_ERROR "trust_add_test_suite(${_name}): COMMAND is required")
    endif()
    if(NOT T_TIMEOUT)
        message(FATAL_ERROR "trust_add_test_suite(${_name}): TIMEOUT is required (tests must never run unbounded)")
    endif()

    add_test(NAME ${_name} COMMAND ${T_COMMAND})

    set(_props TIMEOUT "${T_TIMEOUT}")
    if(T_WORKING_DIRECTORY)
        list(APPEND _props WORKING_DIRECTORY "${T_WORKING_DIRECTORY}")
    endif()
    if(T_FIXTURES_REQUIRED)
        list(APPEND _props FIXTURES_REQUIRED "${T_FIXTURES_REQUIRED}")
    endif()
    if(T_FIXTURES_SETUP)
        list(APPEND _props FIXTURES_SETUP "${T_FIXTURES_SETUP}")
    endif()
    if(T_LABELS)
        list(APPEND _props LABELS "${T_LABELS}")
    endif()
    if(T_RUN_SERIAL)
        list(APPEND _props RUN_SERIAL TRUE)
    endif()
    set_tests_properties(${_name} PROPERTIES ${_props})
endfunction()
