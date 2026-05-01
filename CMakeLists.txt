cmake_minimum_required(VERSION 3.28)

# ── Version & git hash ──
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION_RAW)
string(STRIP "${PROJECT_VERSION_RAW}" PROJECT_VERSION)

string(REPLACE "." ";" VERSION_PARTS "${PROJECT_VERSION}")
list(GET VERSION_PARTS 0 TRUST_VERSION_MAJOR)
list(GET VERSION_PARTS 1 TRUST_VERSION_MINOR)
list(GET VERSION_PARTS 2 TRUST_VERSION_PATCH)

set(TRUST_VERSION "${PROJECT_VERSION}")

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE TRUST_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(TRUST_VERSION_FULL "${TRUST_VERSION}-${TRUST_GIT_HASH}")

# ── Compilers ──
set(CLANG_VERSION 22)
set(CMAKE_C_COMPILER clang-${CLANG_VERSION} CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER clang++-${CLANG_VERSION} CACHE FILEPATH "" FORCE)

project(transpiler LANGUAGES C CXX)

# ── Global settings ──
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_compile_options(-Wswitch -Werror)

# ── Options ──
option(WITH_TORCH "Enable libtorch support (builds libtensor_cpu.so)" OFF)

# ── Common paths ──
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(WITH_TORCH)
    set(LIBTORCH_ROOT "${CMAKE_SOURCE_DIR}/contrib/libtorch")
    set(LIBTORCH_INCLUDE_DIRS
        ${LIBTORCH_ROOT}/include
        ${LIBTORCH_ROOT}/include/torch/csrc/api/include
    )
    set(LIBTORCH_LIB_DIRS ${LIBTORCH_ROOT}/lib)
endif()

# ── Required tools ──
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(GTest REQUIRED)
find_program(FLEX_EXECUTABLE flex REQUIRED)
find_program(BISON_EXECUTABLE bison REQUIRED)
find_program(LIT_EXECUTABLE NAMES lit REQUIRED)

# ── Required libraries ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(GMP REQUIRED gmp)

find_package(nlohmann_json REQUIRED)

# ── LLVM/Clang (for stdlib) ──
list(APPEND CMAKE_PREFIX_PATH "/usr/lib/llvm-22/lib/cmake")
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# ── GMP interface target ──
add_library(gmp_interface INTERFACE)
target_include_directories(gmp_interface INTERFACE ${GMP_INCLUDE_DIRS})
target_link_libraries(gmp_interface INTERFACE ${GMP_LIBRARIES})

# ── Configuration display ──
message(STATUS "═══════════════════════════════════════════════════")
message(STATUS "Project Configuration:")
message(STATUS "  C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  C Compiler:   ${CMAKE_C_COMPILER}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Dir:    ${CMAKE_BINARY_DIR}")
message(STATUS "  Python3:      ${Python3_EXECUTABLE} (${Python3_VERSION})")
message(STATUS "  lit:          ${LIT_EXECUTABLE}")
message(STATUS "  Flex:         ${FLEX_EXECUTABLE}")
message(STATUS "  Bison:        ${BISON_EXECUTABLE}")
message(STATUS "  nlohmann_json: ${nlohmann_json_VERSION}")
message(STATUS "  LLVM:         ${LLVM_VERSION}")
message(STATUS "  Version:      ${TRUST_VERSION_FULL}")
message(STATUS "═══════════════════════════════════════════════════")

# ── Generate files ──
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/trust/version.h.in.cmake
    ${CMAKE_BINARY_DIR}/include/trust/version.h
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cli/options.h.in
    ${CMAKE_BINARY_DIR}/include/cli/options.h
    @ONLY
)
add_custom_target(generate_options_h
    DEPENDS "${CMAKE_BINARY_DIR}/include/cli/options.h"
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/lit.cfg.py.in
    ${CMAKE_BINARY_DIR}/tests/lit.cfg.py
    @ONLY
)
add_custom_target(generate_lit_cfg_py
    DEPENDS "${CMAKE_BINARY_DIR}/tests/lit.cfg.py"
)

# ── LIT output symlinks ──
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
file(CREATE_LINK
    ${CMAKE_BINARY_DIR}/tests/lit.cfg.py
    ${CMAKE_SOURCE_DIR}/tests/lit.cfg.py
    SYMBOLIC
)

# ── Interface targets ──
add_library(trust_include_dirs INTERFACE)
target_include_directories(trust_include_dirs INTERFACE ${PROJECT_INCLUDE_DIR})

# ── Subdirectories ──
add_subdirectory(include/trust)
add_subdirectory(include/dap)

if(WITH_TORCH)
    add_subdirectory(include/precompiled)
endif()

add_subdirectory(include/diag)
add_subdirectory(include/stdlib)
add_subdirectory(include/types)
add_subdirectory(include/parser)
add_subdirectory(include/gencpp)
add_subdirectory(include/cli)
add_dependencies(trust_cli generate_options_h)
add_subdirectory(include/runtime)
add_subdirectory(include/vscode)

# ── Unit tests ──
add_subdirectory(unittest)

# ── LIT test targets ──
if(WITH_TORCH)
    set(TORCH_PCH_DEP torch_pch_target)
else()
    set(TORCH_PCH_DEP "")
endif()

add_custom_target(run_lit_gencpp
    COMMAND ${LIT_EXECUTABLE} --succinct ${CMAKE_SOURCE_DIR}/tests/gencpp
    DEPENDS gencpp_from_ast ${TORCH_PCH_DEP} generate_lit_cfg_py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running LIT tests (gencpp)"
    USES_TERMINAL
)

add_custom_target(run_lit_cli
    COMMAND ${LIT_EXECUTABLE} --succinct ${CMAKE_SOURCE_DIR}/tests/cli
    DEPENDS trust_cli generate_lit_cfg_py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running LIT tests (cli)"
    USES_TERMINAL
)

add_custom_target(run_lit_tests
    DEPENDS run_lit_gencpp run_lit_cli
    COMMENT "Running all LIT tests"
)

add_custom_target(run_tests
    DEPENDS run_unit_tests run_lit_tests
    COMMENT "Running all tests (unit + lit)"
)