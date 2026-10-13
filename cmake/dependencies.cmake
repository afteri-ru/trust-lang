# cmake/dependencies.cmake
# Find required tools, libraries and create utility targets

# -- Required tools --
find_package(GTest REQUIRED)
find_program(FLEX_EXECUTABLE flex REQUIRED)
find_program(BISON_EXECUTABLE NAMES bison REQUIRED)
# `lit` в дистрибутивах LLVM ставится как `lit-<version>` (например /usr/bin/lit-22),
# безымянный `lit` есть не всегда — принимаем оба варианта.
find_program(LIT_EXECUTABLE NAMES lit lit-${CLANG_VERSION} REQUIRED)

# -- Required libraries --
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(GMP REQUIRED gmp)

find_package(nlohmann_json REQUIRED)

# -- toml++ (TOML-парсер/сериализатор для метаинформации пакетов) --
find_package(tomlplusplus REQUIRED)
# Заголовки toml++ - сторонние: помечаем их SYSTEM, чтобы -Werror/-Wold-style-cast
# не срабатывал на них (аналогично msgpack ниже).
get_target_property(_tomlplusplus_include_dirs tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
if(_tomlplusplus_include_dirs)
    set_target_properties(tomlplusplus::tomlplusplus PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_tomlplusplus_include_dirs}"
    )
endif()

# -- msgpack-c (для бинарного source mapping) - статическая линковка --
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

# -- LLVM (for MD5/ArrayRef utilities) --
list(APPEND CMAKE_PREFIX_PATH "/usr/lib/llvm-${CLANG_VERSION}/lib/cmake")
find_package(LLVM REQUIRED CONFIG)

# -- GMP interface target --
add_library(gmp_interface INTERFACE)
target_include_directories(gmp_interface SYSTEM INTERFACE ${GMP_INCLUDE_DIRS})
target_link_libraries(gmp_interface INTERFACE ${GMP_LIBRARIES})

# -- Z3 (optional SMT solver for formal verification) --
if(WITH_SOLVER)
    find_package(Z3 REQUIRED)
    message(STATUS "Z3: ${Z3_VERSION_STRING} at ${Z3_INCLUDE_DIR}")
endif()

# -- ccache (optional compilation cache for repeated/incremental builds) --
# Cache lives in <root>/.cache/ccache (the `.cache` line is already in .gitignore). The compiler
# launcher goes through a GENERATED wrapper (_build/ccache-wrapper.sh) so that CCACHE_DIR /
# CCACHE_BASEDIR / CCACHE_MAXSIZE take effect at build time, not at configure time.
option(TRUST_USE_CCACHE "Cache compilation through ccache (if available)" ON)
set(TRUST_CCACHE_MAXSIZE "20G" CACHE STRING "ccache: maximum cache size (see ccache -x)")
set(TRUST_CCACHE_LAUNCHER "off")
if(TRUST_USE_CCACHE)
    find_program(TRUST_CCACHE_EXECUTABLE NAMES ccache)
    if(TRUST_CCACHE_EXECUTABLE)
        set(TRUST_CCACHE_DIR "${CMAKE_SOURCE_DIR}/.cache/ccache")
        file(MAKE_DIRECTORY "${TRUST_CCACHE_DIR}")
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/ccache-wrapper.sh.in"
            "${CMAKE_BINARY_DIR}/ccache-wrapper.sh"
            @ONLY
        )
        file(CHMOD "${CMAKE_BINARY_DIR}/ccache-wrapper.sh"
             PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_BINARY_DIR}/ccache-wrapper.sh")
        set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_BINARY_DIR}/ccache-wrapper.sh")
        set(TRUST_CCACHE_LAUNCHER "${TRUST_CCACHE_DIR} (max ${TRUST_CCACHE_MAXSIZE})")
        message(STATUS "ccache: ${TRUST_CCACHE_EXECUTABLE} -> ${TRUST_CCACHE_DIR} (max ${TRUST_CCACHE_MAXSIZE})")
    else()
        message(STATUS "ccache: TRUST_USE_CCACHE=ON, но ccache не найден — сборка идёт без кэша")
    endif()
endif()
