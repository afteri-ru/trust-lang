# include/stdlib/CMakeLists.txt
# Standard library component: TrustLang sources (stdlib/*.src) and headers of library types
# (stdlib/*.hpp). The assets themselves are described and embedded by the `assets` component
# (src/assets/asset_provider.cpp, catalog include/assets/asset_catalog.hpp) and published under
# the `@stdlib/…` prefix. stdlib_lib is header-only (the sources are embedded assets).

add_library(stdlib_lib INTERFACE)
target_link_libraries(stdlib_lib INTERFACE trust_include_dirs assets_lib)

