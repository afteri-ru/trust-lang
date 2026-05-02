# test/unit/CMakeLists.txt — единый исполняемый файл для всех юнит-тестов

# ── All test sources ──
set(TEST_SOURCES
    unittest.cpp
    diag/diag_test.cpp
    diag/context_test.cpp
    diag/options_test.cpp
    diag/mapping_test.cpp
    parser/lexer_test.cpp
    parser/lexer_integration_test.cpp
    parser/mmproc_test.cpp
    parser/naming_test.cpp
    parser/parser_test.cpp
    #  parser/token_gen_test.cpp
    cli/cli_test.cpp
    cli/cliopt_test.cpp
    types/registry_test.cpp
    types/alias_test.cpp
    types/cast_test.cpp
    types/numbers_test.cpp
    types/integers_test.cpp
    runtime/rational_test.cpp
    stdlib/name_utils_test.cpp
    stdlib/api_comparator_test.cpp
    debug/trust_source_test.cpp
    debug/transpile_test.cpp
    debug/dap_test.cpp

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
    target_include_directories(unit_tests PRIVATE ${LIBTORCH_INCLUDE_DIRS})
    target_link_directories(unit_tests PRIVATE ${LIBTORCH_LIB_DIRS})
    target_link_libraries(unit_tests PRIVATE torch c10 torch_cpu)
    target_compile_options(unit_tests PRIVATE -frtti)
    target_sources(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/runtime/tensor.cpp)
endif()

# ── Link all libraries ──
target_link_libraries(unit_tests PRIVATE
    gencpp_lib
    diag_lib
    parser_lib
    types_lib
    cli_lib
    stdlib_lib
    debug_lib
    GTest::gtest
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/test/lit/gencpp
    ${MSGPACK_INCLUDE_DIRS}
)

# ── Attach trust module ──
target_trust_module(unit_tests)

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

# lldb_debuggee — standalone программа, используемая как debuggee
add_executable(lldb_debuggee
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/lldb_debuggee.cpp
)
target_compile_options(lldb_debuggee PRIVATE -g -fno-pie)
target_link_options(lldb_debuggee PRIVATE -fno-pie -no-pie)

# simple_transpiler — transpile .src → .cpp + source map
add_executable(simple_transpiler
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/simple_transpiler_main.cpp
)
target_include_directories(simple_transpiler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/debug
)
target_link_libraries(simple_transpiler PRIVATE debug_lib)
# lldb_main — test TrustDebug without DAP
add_executable(lldb_main
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/lldb_main.cpp
)
target_link_libraries(lldb_main PRIVATE debug_lib)
target_include_directories(lldb_main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/debug
)
# simple_integration — integration test (transpile + compile + debug)
add_executable(simple_integration
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/simple_integration_main.cpp
)
target_link_libraries(simple_integration PRIVATE debug_lib)
target_include_directories(simple_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/debug
)
# ── Run unit tests target ──
add_custom_target(run_unit_tests
    COMMAND $<TARGET_FILE:unit_tests> --gtest_brief=1
    DEPENDS unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all unit tests"
    USES_TERMINAL
)

