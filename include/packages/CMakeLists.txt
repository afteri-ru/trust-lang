# include/packages/CMakeLists.txt
# `packages` - shared model of modules and packages (source trees, placement/search,
# `.trust` pack/unpack, manifest document, integrity), shared by the package manager
# and the compiler. Leaf relative to the compiler: no diag/Context.

add_library(packages_lib STATIC
    ${PROJECT_SOURCE_DIR}/packages/module_path.cpp
    ${PROJECT_SOURCE_DIR}/packages/source_tree.cpp
    ${PROJECT_SOURCE_DIR}/packages/package.cpp
    ${PROJECT_SOURCE_DIR}/packages/locate.cpp
    ${PROJECT_SOURCE_DIR}/packages/archive.cpp
    ${PROJECT_SOURCE_DIR}/packages/manifest.cpp
)

target_link_libraries(packages_lib PUBLIC trust_include_dirs utils_lib nlohmann_json::nlohmann_json)
target_link_libraries(packages_lib PRIVATE PkgConfig::ARCHIVE)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(packages_lib PRIVATE trust_pch)

