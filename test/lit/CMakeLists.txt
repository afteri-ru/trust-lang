# test/lit/CMakeLists.txt
# LIT output symlinks and CTest registration

# -- LIT output symlinks (one per test subdirectory) --
# Subdirectories are discovered AUTOMATICALLY from the actual test files (.src/.ast),
# so there is no manual directory list to maintain: any newly grouped subdirectory is
# picked up on reconfigure (CONFIGURE_DEPENDS re-runs the glob when test files change).
# Generated Output/.trust directories are excluded from discovery.
if(NOT CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    file(GLOB_RECURSE _lit_tests CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/test/lit/*.src"
        "${CMAKE_SOURCE_DIR}/test/lit/*.ast"
    )
    set(_lit_dirs)
    foreach(_t IN LISTS _lit_tests)
        get_filename_component(_d "${_t}" DIRECTORY)
        if(NOT _d MATCHES "/Output/" AND NOT _d MATCHES "/\\.trust$")
            list(APPEND _lit_dirs "${_d}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _lit_dirs)
    foreach(_d IN LISTS _lit_dirs)
        file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}/test/lit" "${_d}")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test/lit/Output/${_rel}/Output")
        if(NOT EXISTS "${_d}/Output")
            file(CREATE_LINK
                "${CMAKE_BINARY_DIR}/test/lit/Output/${_rel}/Output"
                "${_d}/Output"
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

# -- CTest registration --
# LIT integration tests run as a single CTest test (via the trust CLI + FileCheck).
# LIT always runs in succinct (brief) mode: `-v` (verbose) is deliberately
# NOT used so the output stays concise.
trust_add_test_suite(lit_tests
    TIMEOUT 300
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test/lit
    # Final stage examples_tests/package tests run only after the early suites pass.
    FIXTURES_SETUP "${TRUST_SUITE_FIXTURES}"
    COMMAND ${LIT_EXECUTABLE} --succinct ${CMAKE_SOURCE_DIR}/test/lit
)
