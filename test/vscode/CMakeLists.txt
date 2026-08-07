# tests/vscode/CMakeLists.txt
# VSCode extension unit and integration tests

find_program(NODE node)
find_program(NPX npx)

# ── CTest registration ──
# Each VSCode suite is a single CTest test.
add_test(NAME vscode_unit
    COMMAND timeout --kill-after=10 120 ${NPX} jest --config ${CMAKE_CURRENT_SOURCE_DIR}/extension/jest.config.js
)
set_tests_properties(vscode_unit PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extension
    TIMEOUT 120
    # Final stage examples_tests runs only after all suites pass (fixture examples_deps).
    FIXTURES_SETUP examples_deps
)

add_test(NAME vscode_dap
    COMMAND timeout --kill-after=10 60 ${NODE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/dap-protocol.test.js
            --dap-path ${CMAKE_BINARY_DIR}/trust-dap
            --src "${TEST_SRC}"
            --cpp "${TEST_CPP}"
            --map "${TEST_MAP}"
            --binary "${TEST_BINARY}"
)
set_tests_properties(vscode_dap PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration
    TIMEOUT 60
    # Final stage examples_tests runs only after all suites pass (fixture examples_deps).
    FIXTURES_SETUP examples_deps
)

add_test(NAME vscode_lsp
    COMMAND timeout --kill-after=10 60 ${NODE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/lsp-protocol.test.js
            --lsp-path ${CMAKE_BINARY_DIR}/trust-lsp
            --src "${TEST_SRC}"
)
set_tests_properties(vscode_lsp PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration
    TIMEOUT 60
    # Final stage examples_tests runs only after all suites pass (fixture examples_deps).
    FIXTURES_SETUP examples_deps
)
