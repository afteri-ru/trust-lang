# test/pm/CMakeLists.txt
# Integration test of the trust-pm executable: pack -> resolve -> fetch/install
# -> offline install from cache. Registered after test/integration (so it runs
# after the unit/lit/vscode/examples/integration stages).

add_executable(pm_integration
    ${CMAKE_CURRENT_SOURCE_DIR}/integration.cpp
)

set_target_properties(pm_integration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(pm_integration trust-pm)

add_test(NAME pm_integration_tests
    COMMAND $<TARGET_FILE:pm_integration>
            $<TARGET_FILE:trust-pm>
            $<TARGET_FILE_DIR:pm_integration>
)
set_tests_properties(pm_integration_tests PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
)
