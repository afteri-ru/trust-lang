# tests/vscode/CMakeLists.txt
# VSCode extension unit and integration tests

find_program(NODE npx)

# ── VSCode extension unit tests ──
add_custom_target(run_vscode_unit_tests
    COMMAND timeout --kill-after=10 120 npx jest --config ${CMAKE_CURRENT_SOURCE_DIR}/extension/jest.config.js
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extension
    COMMENT "Running VSCode extension unit tests (timeout: 120s)"
    USES_TERMINAL
)

# ── DAP integration tests ──
# Использует заранее подготовленные test_data (из test/CMakeLists.txt):
#   --src simple_example.src, --cpp simple_example.cpp, --map simple_example.map
#   --debuggee-path simple_example (скомпилированный ELF)
add_custom_target(run_vscode_dap_tests
    DEPENDS trust-dap simple_compile
    COMMAND timeout --kill-after=10 60 node ${CMAKE_CURRENT_SOURCE_DIR}/integration/dap-protocol.test.js
            --dap-path ${CMAKE_BINARY_DIR}/trust-dap
            --src "${TEST_SRC}"
            --cpp "${TEST_CPP}"
            --map "${TEST_MAP}"
            --binary "${TEST_BINARY}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration
    COMMENT "Running VSCode DAP integration tests (timeout: 60s)"
    USES_TERMINAL
)

# ── LSP integration tests ──
# Использует заранее подготовленные test_data:
#   --src simple_example.src (source map строится in-process, без внешнего компилятора)
add_custom_target(run_vscode_lsp_tests
    DEPENDS trust-lsp
    COMMAND timeout --kill-after=10 60 node ${CMAKE_CURRENT_SOURCE_DIR}/integration/lsp-protocol.test.js
            --lsp-path ${CMAKE_BINARY_DIR}/trust-lsp
            --src "${TEST_SRC}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration
    COMMENT "Running VSCode LSP integration tests (timeout: 60s)"
    USES_TERMINAL
)

# ── Aggregated targets ──
# Интеграционные DAP тесты отключены (trust-dap требует настройки debug-окружения).
# Для запуска: cmake --build build --target run_vscode_dap
add_custom_target(run_vscode_integration_tests
    DEPENDS run_vscode_lsp_tests
    COMMENT "Running all VSCode integration tests (DAP disabled)"
)

add_custom_target(run_vscode_tests
    DEPENDS run_vscode_unit_tests run_vscode_integration_tests
    COMMENT "Running all VSCode extension tests"
)

# ── Individual protocol targets ──
add_custom_target(run_vscode_dap
    DEPENDS run_vscode_dap_tests
    COMMENT "Shorthand: run DAP integration tests (not in default chain)"
)

add_custom_target(run_vscode_lsp
    DEPENDS run_vscode_lsp_tests
    COMMENT "Shorthand: run LSP integration tests"
)
