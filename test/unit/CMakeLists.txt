# test/unit/CMakeLists.txt — единый исполняемый файл для всех юнит-тестов

# ── All test sources ──
set(TEST_SOURCES
    unittest.cpp
    diag/diag_test.cpp
    diag/context_test.cpp
    diag/options_test.cpp
    diag/mapping_test.cpp
    diag/mapping_extended_test.cpp
    diag/save_and_read_test.cpp
    ast/attr_test.cpp
    parser/lexer_test.cpp
    parser/lexer_integration_test.cpp
    parser/mmproc_test.cpp
    parser/naming_test.cpp
    parser/parser_test.cpp
    #  parser/token_gen_test.cpp
    pipeline/pipeline_test.cpp
    # pipeline/pipelineopt_test.cpp
    types/registry_test.cpp
    types/alias_test.cpp
    types/cast_test.cpp
    types/numbers_test.cpp
    types/integers_test.cpp
    runtime/rational_test.cpp
    stdlib/name_utils_test.cpp
    stdlib/api_comparator_test.cpp
    debug/transpile_test.cpp
    debug/dap_test.cpp
    debug/dap_handler_test.cpp
    debug/dap_handler_extended_test.cpp
    lsp/trust_lsp_test.cpp
    utils/cache_test.cpp
    utils/strings_test.cpp
    # gencpp_map_test.cpp

    # Generated lexer
    "${CMAKE_BINARY_DIR}/include/parser/lex.yy.cpp"
)

# Tensor tests (require WITH_TORCH)
if(WITH_TORCH)
    list(APPEND TEST_SOURCES runtime/tensor_test.cpp)
endif()

add_executable(unit_tests ${TEST_SOURCES})


# ── Torch configuration for unit_tests ──
if(WITH_TORCH)
    target_compile_definitions(unit_tests PRIVATE TRUST_HAS_TORCH)
    target_include_directories(unit_tests SYSTEM PRIVATE ${LIBTORCH_INCLUDE_DIRS})
    target_link_directories(unit_tests PRIVATE ${LIBTORCH_LIB_DIRS})
    target_link_libraries(unit_tests PRIVATE torch c10 torch_cpu)
    target_compile_options(unit_tests PRIVATE -frtti)
    target_sources(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/runtime/tensor.cpp)
endif()

# ── Link all libraries ──
target_link_libraries(unit_tests PRIVATE
    # gencpp_lib
    ast_lib
    diag_lib
    parser_lib
    types_lib
    pipeline_lib
    stdlib_lib
    debug_lib
    lsp_lib
    CLI11::CLI11
    GTest::gtest
)

# ── Test data directory ──
# TEST_DATA_DIR определён как CACHE-переменная в корневом CMakeLists.txt.
# Локально не переопределяем — используем глобальную.
file(MAKE_DIRECTORY "${TEST_DATA_DIR}")

target_include_directories(unit_tests PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    # ${CMAKE_SOURCE_DIR}/test/lit/gencpp
)
target_include_directories(unit_tests SYSTEM PRIVATE
    ${MSGPACK_INCLUDE_DIRS}
)

target_compile_definitions(unit_tests PRIVATE
    TEST_DATA_DIR="${TEST_DATA_DIR}"
)

# DapHandler integration tests need prepared ELF binary
add_dependencies(unit_tests prepare_test_data)

# ── Attach trust module ──
target_trust_module(unit_tests)

set_source_files_properties(
    "${CMAKE_BINARY_DIR}/include/parser/lex.yy.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-old-style-cast"
)
target_compile_options(unit_tests PRIVATE
    -Wno-unused-result
)

# ── Symlink tensor plugin for runtime loading ──
add_custom_command(TARGET unit_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/libtensor_cpu.so
        ${CMAKE_CURRENT_BINARY_DIR}/libtensor_cpu.so
    COMMENT "Creating symlink for libtensor_cpu.so"
)

# ── Integration test executables for debug component ──

# simple_transpiler — transpile .src → .cpp + source map
add_executable(simple_transpiler
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/simple_transpiler_main.cpp
)
target_include_directories(simple_transpiler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/debug
)
target_link_libraries(simple_transpiler PRIVATE debug_lib lsp_lib)

# gdb_debuggee — standalone программа, используемая как debuggee для GDB
add_executable(gdb_debuggee
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/gdb_debuggee.cpp
)
target_compile_options(gdb_debuggee PRIVATE -g -fno-pie)
target_link_options(gdb_debuggee PRIVATE -fno-pie -no-pie)

# gdb_main — test GDB via GdbDebug wrapper
add_executable(gdb_main
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/gdb_main.cpp
)
target_link_libraries(gdb_main PRIVATE debug_lib)
target_include_directories(gdb_main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/debug
    ${CMAKE_SOURCE_DIR}/include
)
# ── Run unit tests target ──
add_custom_target(run_unit_tests
    COMMAND "${CMAKE_SOURCE_DIR}/test/wrapper/failed.sh" "run_unit_tests" $<TARGET_FILE:unit_tests> --gtest_brief=1
    DEPENDS unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all unit tests"
    USES_TERMINAL
)
