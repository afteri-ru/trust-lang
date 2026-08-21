# test/unit/CMakeLists.txt - единый исполняемый файл для всех юнит-тестов

# -- All test sources --
set(TEST_SOURCES
    unittest.cpp
    diag/diag_test.cpp
    diag/context_test.cpp
    diag/options_test.cpp
    diag/mapping_test.cpp
    diag/mapping_extended_test.cpp
    diag/save_and_read_test.cpp
    transpiler/e2e/src_map_cli_readback_test.cpp
    attr/attr_test.cpp
    ast/visit_test.cpp
    pipeline/module_registry_test.cpp
    pipeline/module_loader_test.cpp
    pipeline/module_export_test.cpp
    #  parser/token_gen_test.cpp
    pipeline/pipeline_test.cpp
    pipeline/pipeline_symbol_test.cpp
    pipeline/compile_test.cpp
    semantic/semantic_test.cpp
    semantic/symbol_collector_test.cpp
    semantic/scope_test.cpp
    semantic/scope_liveness_test.cpp
    semantic/multilevel_var_test.cpp
    semantic/format_check_test.cpp
    transpiler/transpiler_test.cpp
    transpiler/integration/transpile_lsp_test.cpp
    # pipeline/pipelineopt_test.cpp
    types/numbers_test.cpp
    types/integers_test.cpp
    types/type_alias_test.cpp
    types/type_helpers_test.cpp
    types/reftype_test.cpp
    types/type_method_test.cpp
    types/array_type_test.cpp
    types/runtime_symbols_test.cpp
    runtime/rational_test.cpp
    runtime/dict_test.cpp
    runtime/range_test.cpp
    runtime/checked_cast_test.cpp
    runtime/any_convert_test.cpp
    ${PROJECT_SOURCE_DIR}/runtime/rational.cpp
    lsp/trust_lsp_test.cpp
    lsp/html_emit_test.cpp
    lsp/builtin_catalog_test.cpp
    playground/server_test.cpp
    playground/config_test.cpp
    playground/dispatch_test.cpp
    playground/security_test.cpp
    playground/worker_test.cpp
    utils/cache_test.cpp
    utils/file_io_test.cpp
    utils/strings_test.cpp
    utils/transport_test.cpp
    utils/elf_archive_test.cpp
    utils/error_test.cpp
    utils/io_test.cpp
    solver/smt_printer_test.cpp
    # Syntax tests
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/lexer_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/macro_test.cpp
    ${CMAKE_SOURCE_DIR}/test/unit/syntax/parser_test.cpp
)

# Tensor tests (require WITH_TORCH)
if(WITH_TORCH)
    list(APPEND TEST_SOURCES runtime/tensor_test.cpp)
endif()

add_executable(unit_tests ${TEST_SOURCES})


# -- Torch configuration for unit_tests --
if(WITH_TORCH)
    target_compile_definitions(unit_tests PRIVATE TRUST_HAS_TORCH)
    target_include_directories(unit_tests SYSTEM PRIVATE ${LIBTORCH_INCLUDE_DIRS})
    target_link_directories(unit_tests PRIVATE ${LIBTORCH_LIB_DIRS})
    target_link_libraries(unit_tests PRIVATE torch c10 torch_cpu)
    target_compile_options(unit_tests PRIVATE -frtti)
    target_sources(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/runtime/tensor.cpp)
endif()

# -- Link all libraries --
target_link_libraries(unit_tests PRIVATE
    ast_lib
    diag_lib
    semantic_lib
    transpiler_lib
    types_lib
    pipeline_lib
    module_loader_lib
    debug_lib
    lsp_lib
    playground_lib
    solver_lib
    syntax_lib
    gmp_interface
    GTest::gtest
)

# -- Test data directory --
# TEST_DATA_DIR определён как CACHE-переменная в корневом CMakeLists.txt.
# Локально не переопределяем - используем глобальную.
file(MAKE_DIRECTORY "${TEST_DATA_DIR}")

target_include_directories(unit_tests PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_BINARY_DIR}/syntax
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_include_directories(unit_tests SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
)

target_compile_definitions(unit_tests PRIVATE
    TEST_DATA_DIR="${TEST_DATA_DIR}"
    TRUST_BINARY_PATH="${CMAKE_BINARY_DIR}/trust"
    TRUST_RUNTIME_STATIC_PATH="${CMAKE_BINARY_DIR}/trust-runtime.a"
    TRUST_RUNTIME_SHARED_PATH="${CMAKE_BINARY_DIR}/trust-runtime.so"
    TRUST_RATIONAL_HPP_PATH="${PROJECT_INCLUDE_DIR}/trust/rational.hpp"
)

# -- Attach trust module --
target_trust_module(unit_tests)

# elf_archive_test needs the built runtime library files to extract headers from.
add_dependencies(unit_tests trust_runtime trust_runtime_static)

target_compile_options(unit_tests PRIVATE
    -Wno-unused-result
    -Wno-error=old-style-cast
    -Wno-error=switch
)

# -- Symlink tensor plugin for runtime loading (only when Torch is enabled) --
if(WITH_TORCH)
    add_custom_command(TARGET unit_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/libtensor_cpu.so
            ${CMAKE_CURRENT_BINARY_DIR}/libtensor_cpu.so
        COMMENT "Creating symlink for libtensor_cpu.so"
    )
endif()

# -- Integration test executables for debug component --

# gdb_debuggee - standalone программа, используемая как debuggee для GDB
add_executable(gdb_debuggee
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/gdb_debuggee.cpp
)
target_include_directories(gdb_debuggee PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(gdb_debuggee PRIVATE utils_lib)
target_compile_options(gdb_debuggee PRIVATE -g -fno-pie)
target_link_options(gdb_debuggee PRIVATE -fno-pie -no-pie)

# gdb_main - test GDB via GdbDebug wrapper
add_executable(gdb_main
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/gdb_main.cpp
)
target_link_libraries(gdb_main PRIVATE debug_lib)
target_include_directories(gdb_main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/debug
    ${CMAKE_SOURCE_DIR}/include
)
# -- CTest registration --
# unit_tests is registered as a single CTest test: progress stays compact
# (one line), and `ctest --output-on-failure` shows the full GTest report
# (including the failing test names) only when the suite fails.
add_test(NAME unit_tests
    COMMAND $<TARGET_FILE:unit_tests>
)
set_tests_properties(unit_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 300
    # Final stage examples_tests runs only after all suites pass (fixture examples_deps).
    FIXTURES_SETUP examples_deps
)