# include/diag/CMakeLists.txt
# Diag library - диагностика и опции (чистый слой: без хранения исходников/маппинга).
# Source map вынесен в sourcemap_lib; фасад Context - в session_lib.

# -- Library --
add_library(diag_lib STATIC
    ${PROJECT_SOURCE_DIR}/diag/diag.cpp
    ${PROJECT_SOURCE_DIR}/diag/options.cpp
    ${PROJECT_SOURCE_DIR}/diag/registry.cpp
)

target_link_libraries(diag_lib PUBLIC trust_include_dirs location_lib utils_lib)



# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(diag_lib PRIVATE trust_pch)
