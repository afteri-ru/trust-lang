# include/assets/CMakeLists.txt
# Единый каталог встраиваемых артефактов (stdlib #embed + trust-runtime ELF) и провайдер
# содержимого compiler-embedded ассетов.

add_library(assets_lib STATIC
    ${PROJECT_SOURCE_DIR}/assets/asset_provider.cpp
)

target_link_libraries(assets_lib PUBLIC utils_lib trust_include_dirs)

# asset_provider.cpp uses C23 #embed (stdlib assets) - suppress the extension warning
# (same as pipeline_lib / lsp_lib / trust-runtime).
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/assets/asset_provider.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-c23-extensions"
)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(assets_lib PRIVATE trust_pch)
