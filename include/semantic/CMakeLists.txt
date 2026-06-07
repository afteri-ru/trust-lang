# include/semantic/CMakeLists.txt
# Semantic analyzer library — symbol table, AST analysis, type checking

add_library(semantic_lib STATIC
    ${PROJECT_SOURCE_DIR}/semantic/symbol_table.cpp
    ${PROJECT_SOURCE_DIR}/semantic/analyzer.cpp
)
target_link_libraries(semantic_lib PUBLIC ast_lib types_lib trust_include_dirs)
