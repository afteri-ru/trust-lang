# tests/vscode/CMakeLists.txt
# VSCode extension unit and integration tests

find_program(NODE node)
find_program(NPX npx)

# -- CTest registration --
# Each VSCode suite is a single CTest test.
trust_add_test_suite(vscode_unit
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extension
    FIXTURES_SETUP "${TRUST_SUITE_FIXTURES}"
    COMMAND timeout --kill-after=10 120 ${NPX} jest --config ${CMAKE_CURRENT_SOURCE_DIR}/extension/jest.config.js
)

if(TRUST_BUILD_DAP)
    trust_add_test_suite(vscode_dap
        TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration
        FIXTURES_SETUP "${TRUST_SUITE_FIXTURES}"
        COMMAND timeout --kill-after=10 60 ${NODE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/dap-protocol.test.js
                --dap-path ${CMAKE_BINARY_DIR}/trust-dap
                --src "${TEST_SRC}"
                --cpp "${TEST_CPP}"
                --map "${TEST_MAP}"
                --binary "${TEST_BINARY}"
    )
endif()

trust_add_test_suite(vscode_lsp
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration
    FIXTURES_SETUP "${TRUST_SUITE_FIXTURES}"
    COMMAND timeout --kill-after=10 60 ${NODE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/lsp-protocol.test.js
            --lsp-path ${CMAKE_BINARY_DIR}/trust-lsp
            --src "${TEST_SRC}"
            --work-dir "${TEST_DATA_DIR}/vscode_lsp"
)
