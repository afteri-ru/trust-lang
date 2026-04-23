# unittest/CMakeLists.txt - единый исполняемый файл для всех юнит-тестов

# ── Парсер: генерация лексера (нужен для тестов) ──
set(PARSER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../include/parser")
set(GEN_DIR "${CMAKE_BINARY_DIR}/include/parser")

# ── Все исходники тестов ──
set(TEST_SOURCES
    # Diag
    diag/diag_test.cpp
    diag/options_test.cpp
    # Parser
    parser/lexer_test.cpp
    parser/lexer_integration_test.cpp
    parser/mmproc_test.cpp
    parser/token_gen_test.cpp
    # Gencpp
    gencpp/ast_nodes_test.cpp
    gencpp/ast_parsing_test.cpp
    gencpp/ast_printing_test.cpp
    gencpp/cpp_generator_test.cpp
    gencpp/gencpp_test.cpp
    gencpp/loop_control_test.cpp
    gencpp/semantic_analyzer_test.cpp
    # Cli
    cli/cli_test.cpp
    cli/cliopt_test.cpp
    # Types
    types/type_info_traits_test.cpp
    # Runtime
    runtime/rational_test.cpp
    runtime/var_test.cpp
    runtime/trusted_cpp_test.cpp
)

# ── Tensor tests (appended when WITH_TORCH=ON) ──
if(WITH_TORCH)
    list(APPEND TEST_SOURCES
        runtime/tensor_test.cpp
        ${CMAKE_SOURCE_DIR}/src/runtime/tensor.cpp
    )
endif()

# ── Добавляем сгенерированный лексер ──
list(APPEND TEST_SOURCES ${GEN_DIR}/lex.yy.cpp)

add_executable(unit_tests ${TEST_SOURCES})

# ── Связываем все библиотеки ──
target_link_libraries(unit_tests PRIVATE
    gencpp_lib
    gencpp_test_lib
    diag_lib
    parser_lib
    runtime_lib
    types_lib
    cli_lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/gencpp
)

# ── Attach runtime module (PCM from include/runtime) ──
set(_runtime_pcm_path "${CMAKE_BINARY_DIR}/include/runtime/trust.runtime.pcm")
set(_runtime_pcm_obj "${CMAKE_BINARY_DIR}/include/runtime/trust.runtime_module.o")
target_sources(unit_tests PRIVATE ${_runtime_pcm_obj})
set_source_files_properties(${_runtime_pcm_obj} PROPERTIES
    EXTERNAL_OBJECT true
    GENERATED true
)

add_dependencies(unit_tests parser_lib_target runtime_pcm_target_all trust_pcm_target_all gencpp_test_lib)
target_compile_options(unit_tests PRIVATE
    -Wno-unused-result
    -fmodule-file=trust.runtime=${_runtime_pcm_path}
    -fmodule-file=trust=${CMAKE_BINARY_DIR}/include/trust/trust.pcm
)

# ── Attach trust module object ──
set(_trust_pcm_obj "${CMAKE_BINARY_DIR}/include/trust/trust_module.o")
target_sources(unit_tests PRIVATE ${_trust_pcm_obj})
set_source_files_properties(${_trust_pcm_obj} PROPERTIES
    EXTERNAL_OBJECT true
    GENERATED true
)

# ── Torch includes for unit tests (needed for Tensor in Obj) ──
if(WITH_TORCH)
    target_include_directories(unit_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/contrib/libtorch/include
        ${CMAKE_SOURCE_DIR}/contrib/libtorch/include/torch/csrc/api/include
    )

    target_link_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/contrib/libtorch/lib)
    target_link_libraries(unit_tests PRIVATE torch c10 torch_cpu)
    target_compile_definitions(unit_tests PRIVATE TRUST_HAS_TORCH)

    add_dependencies(unit_tests torch_pch_target)

    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/runtime/tensor_test.cpp PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/include/precompiled/torch_pch.hpp"
    )
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/runtime/tensor.cpp PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/include/precompiled/torch_pch.hpp"
    )
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/runtime/var_test.cpp PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/include/precompiled/torch_pch.hpp"
    )

    # Plugin path for tests
    target_compile_definitions(unit_tests PRIVATE
        TRUST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins"
    )
endif()

# ── Общесистемный custom target ──
if(WITH_TORCH)
    set(UNIT_TEST_DEPS unit_tests tensor_cpu)
else()
    set(UNIT_TEST_DEPS unit_tests)
endif()

add_custom_target(run_unit_tests
    COMMAND $<TARGET_FILE:unit_tests>
    DEPENDS ${UNIT_TEST_DEPS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all unit tests"
    USES_TERMINAL
)
