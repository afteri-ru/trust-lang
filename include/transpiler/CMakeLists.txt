# include/transpiler/CMakeLists.txt
# C++ transpiler library - generates C++ code from AST
# Декомпозиция: драйвер CppTranspiler + компоненты-эмиттеры (Type/Decl/Stmt/Expr/Contract)
# + общий контекст кодогенерации CppEmitContext. Отдельные TU ускоряют сборку.

trust_component(transpiler
    INTERNAL_DIRS driver emit module type
    PUBLIC_HEADERS transpiler.hpp
    SOURCES
        driver/transpiler.cpp
        emit/emit_ctx.cpp emit/decl_emit.cpp emit/type_decl_emit.cpp emit/decl_var_emit.cpp
        emit/decl_func_emit.cpp emit/stmt_emit.cpp emit/stmt_control_emit.cpp
        emit/destructure_emit.cpp emit/expr_emit.cpp emit/expr_arith_emit.cpp
        emit/expr_call_emit.cpp emit/expr_stackcheck_emit.cpp emit/expr_literal_emit.cpp
        emit/expr_container_emit.cpp emit/member_emit.cpp emit/ref_emit.cpp emit/contract_emit.cpp
        type/type_emit.cpp type/type_include_collector.cpp type/type_name_resolver.cpp type/cpp_type_renderer.cpp
        type/type_cpp_facts_view.cpp
        module/module_emit.cpp
    LINK_PUBLIC analysis_lib ir_lib sourcemap_lib target_lib session_lib trust_include_dirs
    # transpiler.cpp includes syntax/term/term.h → parser.h → generated parser.yy.h
    INCLUDE_PRIVATE ${CMAKE_BINARY_DIR}/syntax
    DEPENDS gen_syntax_parsers
)

# Полнота switch по kind в драйвере кодогена (см. ast/kind_dispatch.hpp).
set_source_files_properties(
    ${TRUST_SRC_DIR}/transpiler/driver/transpiler.cpp
    PROPERTIES COMPILE_OPTIONS "-Wswitch-enum"
)
