cmake_minimum_required(VERSION 3.20)

set(CLANG_VERSION 22)
set(CMAKE_C_COMPILER clang-${CLANG_VERSION} CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER clang++-${CLANG_VERSION} CACHE FILEPATH "" FORCE)

project(transpiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Common paths ──
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── Required tools ──
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(GTest REQUIRED)
find_program(FLEX_EXECUTABLE flex REQUIRED)
find_program(BISON_EXECUTABLE bison REQUIRED)
message(STATUS "  Flex:       ${FLEX_EXECUTABLE}")
message(STATUS "  Bison:      ${BISON_EXECUTABLE}")

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

# ── CLI options generation ──
configure_file(
    include/cli/options.h.in
    ${CMAKE_BINARY_DIR}/include/cli/options.h
    @ONLY
)

# ── LIT configuration ──
configure_file(
    tests/lit.cfg.py.in
    ${CMAKE_BINARY_DIR}/tests/lit.cfg.py
    @ONLY
)

# ── LIT output symlinks (source tree → build tree) ──
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests/Output/gencpp/Output)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests/Output/cli/Output)
file(CREATE_LINK
    ${CMAKE_BINARY_DIR}/tests/Output/gencpp/Output
    ${CMAKE_SOURCE_DIR}/tests/gencpp/Output
    SYMBOLIC
)
file(CREATE_LINK
    ${CMAKE_BINARY_DIR}/tests/Output/cli/Output
    ${CMAKE_SOURCE_DIR}/tests/cli/Output
    SYMBOLIC
)

# ── Interface target for common includes ──
add_library(trust_include_dirs INTERFACE)
target_include_directories(trust_include_dirs INTERFACE ${PROJECT_INCLUDE_DIR})

# ── Helper: add trust include dirs to a target ──
function(target_link_trust_includes target)
    target_include_directories(${target} PRIVATE ${PROJECT_INCLUDE_DIR})
    target_link_libraries(${target} PRIVATE trust_include_dirs)
endfunction()

# ── Submodules ──
add_subdirectory(include/trust)
add_subdirectory(include/diag)
add_subdirectory(include/types)
add_subdirectory(include/parser)
add_subdirectory(include/gencpp)
add_subdirectory(include/precompiled)
add_subdirectory(include/cli)

# ── Unit tests ──
add_subdirectory(unittest/cli)

# ── Aggregate test targets ──
add_custom_target(run_unit_tests
    COMMAND $<TARGET_FILE:diag_unit_tests>
    COMMAND $<TARGET_FILE:parser_unit_tests>
    COMMAND $<TARGET_FILE:gencpp_unit_tests>
    COMMAND $<TARGET_FILE:cli_unit_tests>
    DEPENDS diag_unit_tests parser_unit_tests gencpp_unit_tests cli_unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all unit tests"
    USES_TERMINAL
)

# ── LIT test targets (per-module) ──
add_custom_target(run_lit_gencpp
    COMMAND ${LIT_EXECUTABLE} --succinct ${CMAKE_BINARY_DIR}/tests/gencpp
    DEPENDS gencpp_from_ast torch_pch_target
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running LIT tests (gencpp)"
    USES_TERMINAL
)

add_custom_target(run_lit_cli
    COMMAND ${LIT_EXECUTABLE} --succinct ${CMAKE_BINARY_DIR}/tests/cli
    DEPENDS trust_cli
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running LIT tests (cli)"
    USES_TERMINAL
)

# ── All LIT tests ──
add_custom_target(run_lit_tests
    DEPENDS run_lit_gencpp run_lit_cli
    COMMENT "Running all LIT tests"
)

add_custom_target(run_tests
    COMMAND $<TARGET_FILE:diag_unit_tests>
    COMMAND $<TARGET_FILE:parser_unit_tests>
    COMMAND $<TARGET_FILE:gencpp_unit_tests>
    COMMAND ${LIT_EXECUTABLE} --succinct ${CMAKE_BINARY_DIR}/tests
    DEPENDS diag_unit_tests parser_unit_tests gencpp_unit_tests gencpp_from_ast torch_pch_target trust_cli
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all tests (unit + lit)"
    USES_TERMINAL
)
