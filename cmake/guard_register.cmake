# cmake/guard_register.cmake
# Регистрация CI-гейтов как CTest-тестов: позитивный прогон на реальном дереве и негативный
# self-test на фикстуре (WILL_FAIL), доказывающий, что гейт действительно ловит нарушение.
#
# Использование: include(cmake/guard_register.cmake) + trust_add_guard(<имя> <скрипт> [FIXTURE <фикстура>]).
#
# Таймаут обязателен (AGENTS.md п.10: тесты без лимита запрещены) и задаётся здесь один раз —
# гейт не может быть зарегистрирован без него.

include_guard(GLOBAL)

set(TRUST_GUARD_TIMEOUT 300)

function(trust_add_guard guard_name guard_script)
    cmake_parse_arguments(GUARD "" "" "FIXTURE" ${ARGN})

    trust_add_test_suite(${guard_name}
        TIMEOUT ${TRUST_GUARD_TIMEOUT}
        LABELS guards
        COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/${guard_script})

    if(GUARD_FIXTURE)
        trust_add_test_suite(${guard_name}_selftest
            TIMEOUT ${TRUST_GUARD_TIMEOUT}
            LABELS guards
            COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}/${GUARD_FIXTURE} -P ${CMAKE_SOURCE_DIR}/${guard_script})
        set_tests_properties(${guard_name}_selftest PROPERTIES WILL_FAIL TRUE)
    endif()
endfunction()
