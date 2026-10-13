# include/diag/CMakeLists.txt
# Diag library - диагностика и опции (чистый слой: без хранения исходников/маппинга).
# Source map вынесен в sourcemap_lib; фасад Context - в session_lib.

trust_component(diag
    PUBLIC_HEADERS
        base_diags.hpp diag.hpp diag_set.hpp flag_ids.hpp flag_values.hpp
        options.hpp registry.hpp report.hpp severity.hpp source_text_provider.hpp
    INTERNAL_DIRS decl
    SOURCES diag.cpp options.cpp registry.cpp
    LINK_PUBLIC trust_include_dirs location_lib
    LINK_PRIVATE utils_lib
)
