# test/examples/CMakeLists.txt
# Examples as the FINAL CTest stage.
#
# examples/ contains standalone programs (run/pause individually, e.g. for
# debugging) - NOT lit tests. Each is run one-by-one via `trust --run`.
#
# This subdirectory is added LAST (after test/unit, test/lit, test/vscode), so
# examples_tests is numbered sequentially last in the CTest list. It is also
# gated by the `examples_deps` fixture (FIXTURES_REQUIRED), so it runs only after
# ALL prior suites (package_target_test, unit_tests, lit_tests, vscode_*) complete
# successfully; if any of them fails, ctest marks examples_tests "Not Run".

file(GLOB EXAMPLE_SOURCES "${CMAKE_SOURCE_DIR}/examples/*.src")
list(SORT EXAMPLE_SOURCES)

# Substituted into run_examples.cmake by configure_file(@ONLY).
set(TRUST_BIN "${CMAKE_BINARY_DIR}/trust")
set(RUN_DIR "${CMAKE_BINARY_DIR}/examples_run")
set(EXAMPLES_TIMEOUT 240)

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/run_examples.cmake.in
    ${CMAKE_BINARY_DIR}/cmake/run_examples.cmake
    @ONLY
)

add_test(NAME examples_tests
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake/run_examples.cmake
)
set_tests_properties(examples_tests PROPERTIES
    FIXTURES_REQUIRED examples_deps
    FIXTURES_SETUP playground_integration_deps
    RUN_SERIAL TRUE
    TIMEOUT 600
)
