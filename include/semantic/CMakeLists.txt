# include/semantic/CMakeLists.txt
# Semantic analyzer library - symbol table, AST analysis, type checking

trust_component(semantic
    INTERNAL_DIRS core decl ref typer verify
    PUBLIC_HEADERS analysis_api.hpp diag.hpp solver.hpp semantic_model.hpp
    SOURCES
        core/semantic_model.cpp
        core/pass.cpp core/namespace_scope.cpp core/type_resolver.cpp core/expr_facts.cpp
        core/pass_runner.cpp core/capture_last_result.cpp core/name_resolution.cpp
        core/name_resolution_nodes.cpp core/name_res_check.cpp core/local_name.cpp
        core/macro_expander.cpp core/symbol_collector.cpp
        decl/decl_analyzer.cpp decl/var_decl_analyzer.cpp decl/func_decl_analyzer.cpp
        decl/exported_signature_check.cpp decl/type_completeness.cpp decl/func_signature_check.cpp
        decl/destructure_analyzer.cpp decl/type_decl_analyzer.cpp decl/class_decl_analyzer.cpp
        decl/record_decl_analyzer.cpp
        ref/definite_assignment.cpp ref/stack_check_infer.cpp ref/nativeref.cpp
        ref/borrow_flow.cpp ref/ref_cycle.cpp
        typer/arg_casts.cpp typer/type_set.cpp typer/expr_typer.cpp typer/typer_base.cpp
        typer/binary_typer.cpp typer/container_typer.cpp typer/var_typer.cpp typer/call_typer.cpp
        typer/method_call_typer.cpp typer/ellipsis.cpp typer/ref_typer.cpp typer/access_resolver.cpp
        typer/operator_check.cpp typer/operator_resolve.cpp
        verify/verify_typed_ast.cpp verify/trust_analyzer.cpp verify/lint.cpp verify/format_check.cpp
    LINK_PUBLIC ast_lib types_lib symbols_lib ir_lib analysis_lib session_lib trust_include_dirs
)

# Полнота switch по kind в драйвере обхода (см. ast/kind_dispatch.hpp).
set_source_files_properties(
    ${TRUST_SRC_DIR}/semantic/core/name_resolution.cpp
    PROPERTIES COMPILE_OPTIONS "-Wswitch-enum"
)
