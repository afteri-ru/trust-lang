# test/lit/CMakeLists.txt
# LIT test symlinks, custom targets and aggregated test targets

# ── LIT output symlinks ──
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test/lit/Output/gencpp/Output)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test/lit/Output/cli/Output)
file(CREATE_LINK
    ${CMAKE_BINARY_DIR}/test/lit/Output/gencpp/Output
    ${CMAKE_SOURCE_DIR}/test/lit/gencpp/Output
    SYMBOLIC
)
file(CREATE_LINK
    ${CMAKE_BINARY_DIR}/test/lit/Output/cli/Output
    ${CMAKE_SOURCE_DIR}/test/lit/cli/Output
    SYMBOLIC
)
file(CREATE_LINK
    ${CMAKE_BINARY_DIR}/test/lit/lit.cfg.py
    ${CMAKE_SOURCE_DIR}/test/lit/lit.cfg.py
    SYMBOLIC
)

# ── LIT test targets ──
if(WITH_TORCH)
    set(TORCH_PCH_DEP torch_pch_target)
else()
    set(TORCH_PCH_DEP "")
endif()

add_custom_target(run_lit_gencpp
    COMMAND timeout --kill-after=10 300 ${LIT_EXECUTABLE} --succinct ${CMAKE_SOURCE_DIR}/test/lit/gencpp
    DEPENDS gencpp_from_ast ${TORCH_PCH_DEP} generate_lit_cfg_py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running LIT tests (gencpp, timeout: 300s)"
    USES_TERMINAL
)

add_custom_target(run_lit_cli
    COMMAND timeout --kill-after=10 120 ${LIT_EXECUTABLE} --succinct ${CMAKE_SOURCE_DIR}/test/lit/cli
    DEPENDS trust_cli generate_lit_cfg_py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running LIT tests (cli, timeout: 120s)"
    USES_TERMINAL
)

add_custom_target(run_lit_tests
    DEPENDS run_lit_gencpp # run_lit_cli
    COMMENT "Running all LIT tests"
)

# ── Debug test targets ──

add_custom_target(run_lldb_main
    COMMAND timeout --kill-after=5 10 $<TARGET_FILE:lldb_main>
            "$<TARGET_FILE:lldb_debuggee>"
    DEPENDS lldb_main lldb_debuggee
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running LLDB debug test (timeout: 10s, kill-after: 5s)"
    USES_TERMINAL
)

add_custom_target(run_simple_integration
    COMMAND timeout --kill-after=5 10 $<TARGET_FILE:simple_integration>
            --binary "${TEST_BINARY}"
            --map "${TEST_MAP}"
    DEPENDS simple_integration simple_compile
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running debug integration test (timeout: 10s, kill-after: 5s)"
    USES_TERMINAL
)

add_custom_target(run_debug_tests
    DEPENDS run_simple_integration run_lldb_main
    COMMENT "Running debug component tests"
)

# ── Aggregated test targets ──
# `run_tests` объявлен в корневом CMakeLists.txt
