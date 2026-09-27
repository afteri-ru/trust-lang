# include/pm/CMakeLists.txt
# Package manager (`trust-pm`) - standalone component with a CLI + JSON contract.
# Reuses libraries (not a third-party package manager): libsolv (solver),
# libcurl (network/TLS), nlohmann_json. The `.trust` format (tar+zstd) and the
# source-tree model are provided by the shared `packages` component.

add_library(pm_lib STATIC
    ${PROJECT_SOURCE_DIR}/pm/protocol.cpp
    ${PROJECT_SOURCE_DIR}/pm/semver.cpp
    ${PROJECT_SOURCE_DIR}/pm/manifest.cpp
    ${PROJECT_SOURCE_DIR}/pm/cache.cpp
    ${PROJECT_SOURCE_DIR}/pm/net.cpp
    ${PROJECT_SOURCE_DIR}/pm/resolver.cpp
    ${PROJECT_SOURCE_DIR}/pm/cli.cpp
)

target_link_libraries(pm_lib PUBLIC
    trust_include_dirs
    packages_lib
    nlohmann_json::nlohmann_json
)

target_link_libraries(pm_lib PRIVATE
    PkgConfig::SOLV
    PkgConfig::CURL
)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(pm_lib PRIVATE trust_pch)

# -- trust-pm: self-contained package manager executable --
add_executable(trust-pm
    ${PROJECT_SOURCE_DIR}/pm/main.cpp
)

target_link_libraries(trust-pm PRIVATE
    pm_lib
)
