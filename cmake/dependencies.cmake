# cmake/dependencies.cmake
# Find required tools, libraries and create utility targets

# ── Required tools ──
find_package(ZLIB REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(GTest REQUIRED)
find_program(FLEX_EXECUTABLE flex REQUIRED)
find_program(BISON_EXECUTABLE bison REQUIRED)
find_program(LIT_EXECUTABLE NAMES lit REQUIRED)

# ── Required libraries ──
find_package(PkgConfig REQUIRED)
pkg_check_modules(GMP REQUIRED gmp)

find_package(nlohmann_json REQUIRED)

# ── CLI11 (для парсинга аргументов командной строки) ──
find_package(CLI11 REQUIRED)

# ── msgpack-c (для бинарного source mapping) — статическая линковка ──
pkg_check_modules(MSGPACK REQUIRED msgpack-c)
find_library(MSGPACK_STATIC_LIB
    NAMES libmsgpack-c.a msgpack-c.a
    HINTS ${MSGPACK_LIBRARY_DIRS} /usr/local/lib /usr/lib
)
if(NOT MSGPACK_STATIC_LIB)
    message(FATAL_ERROR "Static library libmsgpack-c.a not found")
endif()
message(STATUS "msgpack-c static lib: ${MSGPACK_STATIC_LIB}")

add_library(msgpack-c-static STATIC IMPORTED)
set_target_properties(msgpack-c-static PROPERTIES
    IMPORTED_LOCATION "${MSGPACK_STATIC_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${MSGPACK_INCLUDE_DIRS}"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${MSGPACK_INCLUDE_DIRS}"
)

# ── LLVM/Clang (for stdlib) ──
list(APPEND CMAKE_PREFIX_PATH "/usr/lib/llvm-22/lib/cmake")
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# ── LLDB (for debug) — версия из CLANG_VERSION ──
set(LLDB_INCLUDE_DIRS "/usr/lib/llvm-${CLANG_VERSION}/include")
set(LLDB_LIBRARIES "/usr/lib/llvm-${CLANG_VERSION}/lib/liblldb.so")

# ── GMP interface target ──
add_library(gmp_interface INTERFACE)
target_include_directories(gmp_interface SYSTEM INTERFACE ${GMP_INCLUDE_DIRS})
target_link_libraries(gmp_interface INTERFACE ${GMP_LIBRARIES})

# ── Z3 (optional SMT solver for formal verification) ──
if(WITH_SOLVER)
    find_package(Z3 REQUIRED)
    message(STATUS "Z3: ${Z3_VERSION_STRING} at ${Z3_INCLUDE_DIR}")
endif()
