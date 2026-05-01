# unittest/CMakeLists.txt — единый исполняемый файл для всех юнит-тестов

set(GEN_DIR "${CMAKE_BINARY_DIR}/include/parser")

# ── All test sources ──
set(TEST_SOURCES
    unittest.cpp
    diag/diag_test.cpp
    diag/options_test.cpp
    parser/lexer_test.cpp
    parser/lexer_integration_test.cpp
    parser/mmproc_test.cpp
    parser/token_gen_test.cpp
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
    dap/integration_test.cpp
)

# Tensor tests (require WITH_TORCH)
if(WITH_TORCH)
    list(APPEND TEST_SOURCES runtime/tensor_test.cpp)
endif()

# ── Add generated lexer ──
list(APPEND TEST_SOURCES ${GEN_DIR}/lex.yy.cpp)

add_executable(unit_tests ${TEST_SOURCES})

# ── Torch configuration for unit_tests ──
if(WITH_TORCH)
    target_compile_definitions(unit_tests PRIVATE TRUST_HAS_TORCH)
    target_include_directories(unit_tests PRIVATE ${LIBTORCH_INCLUDE_DIRS})
    target_link_directories(unit_tests PRIVATE ${LIBTORCH_LIB_DIRS})
    target_link_libraries(unit_tests PRIVATE torch c10 torch_cpu)
    target_compile_options(unit_tests PRIVATE -frtti)
    target_sources(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/runtime/tensor.cpp)
endif()

# ── Link all libraries ──
target_link_libraries(unit_tests PRIVATE
    gencpp_lib
    diag_lib
    parser_lib
    types_lib
    cli_lib
    stdlib_lib
    dap_lib
    GTest::gtest
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/gencpp
)

# ── Attach trust module object ──
set(_trust_pcm_obj "${CMAKE_BINARY_DIR}/include/trust/trust_module.o")
target_sources(unit_tests PRIVATE ${_trust_pcm_obj})
set_source_files_properties(${_trust_pcm_obj} PROPERTIES
    EXTERNAL_OBJECT true
    GENERATED true
)

add_dependencies(unit_tests parser_lib_target trust_pcm_target_all dap_lib)

target_compile_options(unit_tests PRIVATE
    -Wno-unused-result
    -fmodule-file=trust=${CMAKE_BINARY_DIR}/include/trust/trust.pcm
)

# ── Symlink tensor plugin for runtime loading ──
add_custom_command(TARGET unit_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/libtensor_cpu.so
        ${CMAKE_CURRENT_BINARY_DIR}/libtensor_cpu.so
    COMMENT "Creating symlink for libtensor_cpu.so"
)

# ── Run unit tests target ──
add_custom_target(run_unit_tests
    COMMAND $<TARGET_FILE:unit_tests> --gtest_brief=1
    DEPENDS unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all unit tests"
    USES_TERMINAL
)
