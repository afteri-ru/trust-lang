# include/semantic/CMakeLists.txt
# Semantic analyzer library - symbol table, AST analysis, type checking

add_library(semantic_lib STATIC
    ${PROJECT_SOURCE_DIR}/semantic/pass.cpp
    ${PROJECT_SOURCE_DIR}/semantic/pass_runner.cpp
    ${PROJECT_SOURCE_DIR}/semantic/name_resolution.cpp
    ${PROJECT_SOURCE_DIR}/semantic/name_resolution_nodes.cpp
    ${PROJECT_SOURCE_DIR}/semantic/name_res_check.cpp
    ${PROJECT_SOURCE_DIR}/semantic/decl_analyzer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/type_decl_analyzer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/type_set.cpp
    ${PROJECT_SOURCE_DIR}/semantic/class_decl_analyzer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/record_decl_analyzer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/expr_typer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/expr_node_typer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/expr_collect.cpp
    ${PROJECT_SOURCE_DIR}/semantic/ellipsis.cpp
    ${PROJECT_SOURCE_DIR}/semantic/assign_typer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/ref_expr_typer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/access_resolver.cpp
    ${PROJECT_SOURCE_DIR}/semantic/trust_analyzer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/macro_expander.cpp
    ${PROJECT_SOURCE_DIR}/semantic/lint.cpp
    ${PROJECT_SOURCE_DIR}/semantic/stack_check_infer.cpp
    ${PROJECT_SOURCE_DIR}/semantic/format_check.cpp
    ${PROJECT_SOURCE_DIR}/semantic/symbol_collector.cpp
    ${PROJECT_SOURCE_DIR}/semantic/nativeref.cpp
    ${PROJECT_SOURCE_DIR}/semantic/borrow_check.cpp
    ${PROJECT_SOURCE_DIR}/semantic/ref_cycle.cpp
    ${PROJECT_SOURCE_DIR}/semantic/operator_check.cpp
)
target_link_libraries(semantic_lib PUBLIC ast_lib types_lib analysis_lib session_lib sourcemap_lib trust_include_dirs)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(semantic_lib PRIVATE trust_pch)
