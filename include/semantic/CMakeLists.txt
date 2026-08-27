# include/semantic/CMakeLists.txt
# Semantic analyzer library - symbol table, AST analysis, type checking

add_library(semantic_lib STATIC
    ${PROJECT_SOURCE_DIR}/semantic/symbol_table.cpp
    ${PROJECT_SOURCE_DIR}/semantic/pass.cpp
    ${PROJECT_SOURCE_DIR}/semantic/pass_runner.cpp
    ${PROJECT_SOURCE_DIR}/semantic/name_resolution.cpp
    ${PROJECT_SOURCE_DIR}/semantic/decl_analyzer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/expr_typer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/access_resolver.cpp
    ${PROJECT_SOURCE_DIR}/semantic/trust_analyzer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/macro_expander.cpp
    ${PROJECT_SOURCE_DIR}/semantic/lint.cpp
    ${PROJECT_SOURCE_DIR}/semantic/format_check.cpp
    ${PROJECT_SOURCE_DIR}/semantic/symbol_collector.cpp
)
target_link_libraries(semantic_lib PUBLIC ast_lib types_lib trust_include_dirs)
