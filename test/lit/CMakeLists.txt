# test/lit/CMakeLists.txt
# LIT test symlinks, custom targets and aggregated test targets

# ── LIT output symlinks (only needed for out-of-source builds) ──
if(NOT CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
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
endif()

# ── LIT test targets ──
if(WITH_TORCH)
    set(TORCH_PCH_DEP torch_pch_target)
else()
    set(TORCH_PCH_DEP "")
endif()

add_custom_target(run_lit_gencpp
    COMMAND "${CMAKE_SOURCE_DIR}/test/wrapper/failed.sh" "run_lit_gencpp" timeout --kill-after=10 300 ${LIT_EXECUTABLE} --succinct ${CMAKE_SOURCE_DIR}/test/lit/gencpp
    DEPENDS gencpp_from_ast ${TORCH_PCH_DEP} generate_lit_cfg_py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running LIT tests (gencpp, timeout: 300s)"
    USES_TERMINAL
)

# add_custom_target(run_lit_cli
#     COMMAND timeout --kill-after=10 120 ${LIT_EXECUTABLE} --succinct ${CMAKE_SOURCE_DIR}/test/lit/cli
#     DEPENDS trust_cli generate_lit_cfg_py
#     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#     COMMENT "Running LIT tests (cli, timeout: 120s)"
#     USES_TERMINAL
# )

add_custom_target(run_lit_tests
    DEPENDS run_lit_gencpp
    COMMENT "Running all LIT tests"
)

# ── Debug test targets ──

add_custom_target(run_debug_main
    COMMAND "${CMAKE_SOURCE_DIR}/test/wrapper/failed.sh" "run_debug_main" timeout --kill-after=5 10 $<TARGET_FILE:gdb_main> "$<TARGET_FILE:gdb_debuggee>"
    DEPENDS gdb_main gdb_debuggee
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running GDB debug test (timeout: 10s, kill-after: 5s)"
    USES_TERMINAL
)

# add_custom_target(run_simple_integration
#     COMMAND "${CMAKE_SOURCE_DIR}/test/wrapper/failed.sh" "run_simple_integration" timeout --kill-after=5 10 $<TARGET_FILE:simple_integration> --binary "${TEST_BINARY}" --map "${TEST_MAP}"
#     DEPENDS simple_integration simple_compile
#     WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#     COMMENT "Running debug integration test (timeout: 10s, kill-after: 5s)"
#     USES_TERMINAL
# )

# add_custom_target(run_debug_tests
#     DEPENDS run_simple_integration
#     COMMENT "Running debug component tests"
# )

# ── Aggregated test targets ──
# `run_tests` объявлен в корневом CMakeLists.txt
