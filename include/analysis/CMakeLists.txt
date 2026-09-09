# include/analysis/CMakeLists.txt
# Продукты анализа, разделяемые анализатором (semantic) и кодогенерацией (transpiler):
# таблица символов (Symbol/SymbolTable) и индекс символов (SymbolInfo/SymbolIndex).
# Ниже semantic/transpiler по зависимостям (опирается только на ast/types/location).

add_library(analysis_lib STATIC
    ${PROJECT_SOURCE_DIR}/analysis/symbol_table.cpp
)

target_link_libraries(analysis_lib PUBLIC
    ast_lib
    types_lib
    location_lib
    trust_include_dirs
)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(analysis_lib PRIVATE trust_pch)
