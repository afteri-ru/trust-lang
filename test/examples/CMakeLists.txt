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

file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/examples/*.src")
# Тензорные примеры требуют toolchain-возможность with-torch (иначе capability-диагностика).
if(WITH_TORCH)
    file(GLOB TENSOR_EXAMPLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/examples/tensor/*.src")
    list(APPEND EXAMPLE_SOURCES ${TENSOR_EXAMPLE_SOURCES})
endif()
list(SORT EXAMPLE_SOURCES)
# prime.src и fibonacci.src требуют аргументов командной строки (без дефолтов);
# указываем их для автозапуска в examples-гарнессе (см. cmake/run_examples.sh.in).
set(EXAMPLE_RUN_ARGS "prime:2 10;fibonacci:3000")
# Сколько примеров запускать одновременно (bash-гарнесс фоном, wait -n).
set(EXAMPLES_PARALLEL 4 CACHE STRING "parallel examples run concurrently")

# Substituted into run_examples.sh by configure_file(@ONLY).
set(TRUST_BIN "${CMAKE_BINARY_DIR}/trust")
set(RUN_DIR "${CMAKE_BINARY_DIR}/examples_run")
set(EXAMPLES_TIMEOUT 240)

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/run_examples.sh.in
    ${CMAKE_BINARY_DIR}/cmake/run_examples.sh
    @ONLY
)
file(CHMOD ${CMAKE_BINARY_DIR}/cmake/run_examples.sh
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

trust_add_test_suite(examples_tests
    TIMEOUT 600
    FIXTURES_REQUIRED examples_deps
    FIXTURES_SETUP integration_deps
    RUN_SERIAL
    COMMAND bash ${CMAKE_BINARY_DIR}/cmake/run_examples.sh
)
