# test/examples/CMakeLists.txt
# Examples as a late CTest stage.
#
# examples/ contains standalone programs (run/pause individually, e.g. for
# debugging) - NOT lit tests. Each is run one-by-one via `trust --run`.
#
# This subdirectory is added after test/unit, test/lit and test/vscode (and before
# test/integration / test/package - see the root CMakeLists.txt). It is gated by the
# `examples_deps` fixture (FIXTURES_REQUIRED), so it runs only after
# the unit/lit/vscode suites complete successfully; if any of them fails, ctest
# marks examples_tests "Not Run".

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
    FIXTURES_SETUP integration_deps
    RUN_SERIAL TRUE
    TIMEOUT 600
)
