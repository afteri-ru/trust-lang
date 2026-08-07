# test/lit/CMakeLists.txt
# LIT output symlinks and CTest registration

# ── LIT output symlinks (one per test subdirectory) ──
if(NOT CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    foreach(_subdir parser/ast parser/lex parser/tokens parser/diag parser/dsl transpiler/codegen transpiler/src_map pipeline/build pipeline/e2e pipeline/module)
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

# ── CTest registration ──
# LIT integration tests run as a single CTest test (via the trust CLI + FileCheck).
# LIT always runs in succinct (brief) mode: `-v` (verbose) is deliberately
# NOT used so the output stays concise.
add_test(NAME lit_tests
    COMMAND ${LIT_EXECUTABLE} --succinct ${CMAKE_SOURCE_DIR}/test/lit
)
set_tests_properties(lit_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test/lit
    TIMEOUT 300
    # Final stage examples_tests runs only after all suites pass (fixture examples_deps).
    FIXTURES_SETUP examples_deps
)
