cmake_minimum_required(VERSION 3.20)

set(CLANG_VERSION 22)
set(CMAKE_C_COMPILER clang-${CLANG_VERSION} CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER clang++-${CLANG_VERSION} CACHE FILEPATH "" FORCE)

project(transpiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Required tools ──
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(GTest REQUIRED)

# ── Configuration display ──
message(STATUS "═══════════════════════════════════════════════════")
message(STATUS "Project Configuration:")
message(STATUS "  C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  C Compiler:   ${CMAKE_C_COMPILER}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Dir:    ${CMAKE_BINARY_DIR}")
message(STATUS "  Python3:      ${Python3_EXECUTABLE} (${Python3_VERSION})")

find_program(LIT_EXECUTABLE NAMES lit)
if(LIT_EXECUTABLE)
    message(STATUS "  lit:          ${LIT_EXECUTABLE}")
else()
    message(FATAL_ERROR "lit not found — LIT tests are required and cannot be skipped")
endif()
message(STATUS "═══════════════════════════════════════════════════")

# ── Подпроекты (только библиотеки, без тестов) ──
add_subdirectory(include/diag)
add_subdirectory(include/token)

# ── Token unit tests ──
add_executable(token_unit_tests unittest/token/token_gen_test.cpp)
target_link_libraries(token_unit_tests PRIVATE token_lib GTest::gtest GTest::gtest_main)
target_include_directories(token_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/unittest/token
)

add_custom_target(token_test
    COMMAND $<TARGET_FILE:token_unit_tests>
    DEPENDS token_unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running token unit tests..."
    USES_TERMINAL
)

# ── Diag unit tests ──
add_executable(diag_unit_tests
    unittest/diag/diag_test.cpp
    unittest/diag/options_test.cpp
)
target_link_libraries(diag_unit_tests PRIVATE diag_lib GTest::gtest GTest::gtest_main)
target_include_directories(diag_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/unittest/diag
)

add_custom_target(diag_test
    COMMAND $<TARGET_FILE:diag_unit_tests>
    DEPENDS diag_unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running diag unit tests..."
    USES_TERMINAL
)

# ── gencpp: C++ Module infrastructure ──
set(TRUST_PCM "${CMAKE_CURRENT_BINARY_DIR}/gencpp.pcm")
set(TRUST_PCM_OBJ "${CMAKE_CURRENT_BINARY_DIR}/gencpp_module.o")
set(GENCPP_CPPM "${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp.cppm")

set(MODULE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp/types.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp/ast_visitor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp/ast.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp/symbol_table.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp/ast_builder.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp/ast_validator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp/ast_printer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp/cpp_generator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gencpp/semantic_analyzer.hpp
)

set(MODULE_COMPILE_OPTIONS
    -std=c++${CMAKE_CXX_STANDARD}
    -I${CMAKE_CURRENT_SOURCE_DIR}/include
    -I${CMAKE_CURRENT_BINARY_DIR}
    -I${CMAKE_CURRENT_BINARY_DIR}/include
)

# Шаг 1: Precompile модуль в PCM
add_custom_command(
    OUTPUT "${TRUST_PCM}"
    COMMAND ${CMAKE_CXX_COMPILER} ${MODULE_COMPILE_OPTIONS}
            -x c++-module --precompile
            ${GENCPP_CPPM}
            -o ${TRUST_PCM}
    DEPENDS ${GENCPP_CPPM} ${MODULE_HEADERS}
    COMMENT "Precompiling C++${CMAKE_CXX_STANDARD} module -> ${TRUST_PCM}"
    VERBATIM
)

# Шаг 2: Компиляция PCM в объектный файл для линковки
add_custom_command(
    OUTPUT "${TRUST_PCM_OBJ}"
    COMMAND ${CMAKE_CXX_COMPILER} ${MODULE_COMPILE_OPTIONS}
            -fmodule-file=trust=${TRUST_PCM}
            -x c++-module
            ${GENCPP_CPPM}
            -c -o ${TRUST_PCM_OBJ}
    DEPENDS ${TRUST_PCM} ${GENCPP_CPPM}
    COMMENT "Compiling module PCM to object file -> ${TRUST_PCM_OBJ}"
    VERBATIM
)

add_custom_target(gencpp_pcm_target ALL DEPENDS ${TRUST_PCM_OBJ})

# Функция для подключения модуля к таргету
function(target_use_gencpp_module target)
    add_dependencies(${target} gencpp_pcm_target)
    target_sources(${target} PRIVATE ${TRUST_PCM_OBJ})
    set_source_files_properties(${TRUST_PCM_OBJ} PROPERTIES
        EXTERNAL_OBJECT true
        GENERATED true
    )
    target_compile_options(${target} PRIVATE
        -fmodule-file=trust=${TRUST_PCM}
    )
endfunction()

# ── gencpp: Library ──
set(GENCPP_LIB_SRC
    src/gencpp/ast.cpp
    src/gencpp/ast_printer.cpp
    src/gencpp/cpp_generator.cpp
    src/gencpp/semantic_analyzer.cpp
)

add_library(gencpp_lib STATIC ${GENCPP_LIB_SRC})
target_include_directories(gencpp_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gencpp_lib PUBLIC token_lib diag_lib)
add_dependencies(gencpp_lib token_gen_target)

# ── gencpp: Test AST format parser library (test-only) ──
add_library(gencpp_test_lib STATIC
    tests/gencpp/ast_format_parser.cpp
)
target_include_directories(gencpp_test_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/gencpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(gencpp_test_lib PUBLIC gencpp_lib)

# ── gencpp: CLI tool for transpiling from AST files ──
add_executable(gencpp_from_ast tests/gencpp/gencpp_from_ast.cpp)
target_use_gencpp_module(gencpp_from_ast)
target_link_libraries(gencpp_from_ast PRIVATE gencpp_lib gencpp_test_lib)

# ── gencpp: Unit tests ──
set(GENCPP_UNIT_TEST_SOURCES
    unittest/gencpp/ast_nodes_test.cpp
    unittest/gencpp/ast_parsing_test.cpp
    unittest/gencpp/ast_printing_test.cpp
    unittest/gencpp/cpp_generator_test.cpp
    unittest/gencpp/gencpp_test.cpp
    unittest/gencpp/loop_control_test.cpp
    unittest/gencpp/semantic_analyzer_test.cpp
)

add_executable(gencpp_unit_tests ${GENCPP_UNIT_TEST_SOURCES})
target_use_gencpp_module(gencpp_unit_tests)
target_link_libraries(gencpp_unit_tests PRIVATE gencpp_lib gencpp_test_lib GTest::gtest GTest::gtest_main)
target_include_directories(gencpp_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/unittest
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/gencpp
)

add_custom_target(gencpp_unit_test
    COMMAND $<TARGET_FILE:gencpp_unit_tests>
    DEPENDS gencpp_unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running gencpp unit tests..."
    USES_TERMINAL
)

# ── gencpp: LIT tests (always required — failure if lit missing) ──
add_custom_target(gencpp_lit_test
    COMMAND ${LIT_EXECUTABLE} -v ${CMAKE_CURRENT_SOURCE_DIR}/tests/gencpp
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running gencpp LIT tests..."
    USES_TERMINAL
)

# ── Aggregate test targets ──
add_custom_target(all_unit_tests
    COMMAND ${CMAKE_COMMAND} -E echo "=== DIAG TESTS ==="
    COMMAND $<TARGET_FILE:diag_unit_tests>
    COMMAND ${CMAKE_COMMAND} -E echo "=== TOKEN TESTS ==="
    COMMAND $<TARGET_FILE:token_unit_tests>
    COMMAND ${CMAKE_COMMAND} -E echo "=== GENCPP UNIT TESTS ==="
    COMMAND $<TARGET_FILE:gencpp_unit_tests>
    DEPENDS diag_unit_tests token_unit_tests gencpp_unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all unit tests"
    USES_TERMINAL
)

add_custom_target(all_tests
    COMMAND ${CMAKE_COMMAND} -E echo "=== DIAG TESTS ==="
    COMMAND $<TARGET_FILE:diag_unit_tests>
    COMMAND ${CMAKE_COMMAND} -E echo "=== TOKEN TESTS ==="
    COMMAND $<TARGET_FILE:token_unit_tests>
    COMMAND ${CMAKE_COMMAND} -E echo "=== GENCPP UNIT TESTS ==="
    COMMAND $<TARGET_FILE:gencpp_unit_tests>
    COMMAND ${CMAKE_COMMAND} -E echo "=== GENCPP LIT TESTS ==="
    COMMAND ${LIT_EXECUTABLE} -v ${CMAKE_CURRENT_SOURCE_DIR}/tests/gencpp
    DEPENDS diag_unit_tests token_unit_tests gencpp_unit_tests gencpp_from_ast
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all tests (unit + lit)"
    USES_TERMINAL
)