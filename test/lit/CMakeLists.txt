# test/lit/CMakeLists.txt
# LIT test symlinks, custom targets and aggregated test targets

# ── LIT output symlinks (one per test subdirectory) ──
if(NOT CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    foreach(_subdir parser/dsl parser/macros parser pipeline transpiler)
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test/lit/Output/${_subdir}/Output)
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/test/lit/${_subdir}/Output")
            file(CREATE_LINK
                ${CMAKE_BINARY_DIR}/test/lit/Output/${_subdir}/Output
                ${CMAKE_SOURCE_DIR}/test/lit/${_subdir}/Output
                SYMBOLIC
            )
        endif()
    endforeach()
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/test/lit/lit.cfg.py")
        file(CREATE_LINK
            ${CMAKE_BINARY_DIR}/test/lit/lit.cfg.py
            ${CMAKE_SOURCE_DIR}/test/lit/lit.cfg.py
            SYMBOLIC
        )
    endif()
endif()

# ── LIT test targets ──
if(WITH_TORCH)
    set(TORCH_PCH_DEP torch_pch_target)
else()
    set(TORCH_PCH_DEP "")
endif()

# add_custom_target(run_lit_pipeline
#     COMMAND timeout --kill-after=10 120 ${LIT_EXECUTABLE} --succinct ${CMAKE_SOURCE_DIR}/test/lit/cli
#     DEPENDS pipeline_lib generate_lit_cfg_py
#     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#     COMMENT "Running LIT tests (pipeline, timeout: 120s)"
#     USES_TERMINAL
# )

add_custom_target(run_lit_tests
    COMMAND ${LIT_EXECUTABLE} -v ${CMAKE_SOURCE_DIR}/test/lit
    DEPENDS trust generate_lit_cfg_py
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test/lit
    COMMENT "Running LIT tests"
    USES_TERMINAL
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
