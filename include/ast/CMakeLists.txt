# include/ast/CMakeLists.txt
# AST library - token definitions, naming, AST node types and the @[...] attribute parser.
# Pure attribute value/registry types (AttrId/Attr/AttrPool/built-in names) live in attrs_lib.

# -- AST library --
add_library(ast_lib STATIC
    ${PROJECT_SOURCE_DIR}/ast/ident_name.cpp
    ${PROJECT_SOURCE_DIR}/ast/ast_children.cpp
    ${PROJECT_SOURCE_DIR}/ast/ast_base.cpp
    ${PROJECT_SOURCE_DIR}/ast/ast_expr.cpp
    ${PROJECT_SOURCE_DIR}/ast/ast_decl.cpp
    ${PROJECT_SOURCE_DIR}/ast/ast_stmt.cpp
    ${PROJECT_SOURCE_DIR}/ast/ast_contract.cpp
    ${PROJECT_SOURCE_DIR}/ast/token_base.cpp
    ${PROJECT_SOURCE_DIR}/ast/token_type.cpp
    ${PROJECT_SOURCE_DIR}/ast/attr_parser.cpp
    ${PROJECT_SOURCE_DIR}/ast/ref_syntax.cpp
    ${PROJECT_SOURCE_DIR}/ast/term_to_ast.cpp
    ${PROJECT_SOURCE_DIR}/ast/lowering.cpp
)
target_link_libraries(ast_lib PUBLIC diag_lib syntax_lib attrs_lib session_lib sourcemap_lib trust_include_dirs)
target_link_libraries(ast_lib PRIVATE trust_llvm_dirs)
# syntax/term.h includes syntax/parser.h → generated parser.yy.h
target_include_directories(ast_lib PRIVATE "${CMAKE_BINARY_DIR}/syntax")
# term_to_ast.cpp includes syntax headers (term.h → parser.h) that trigger
# -Wswitch / old-style-cast warnings - suppress as errors.
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/ast/term_to_ast.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-error=switch;-Wno-error=old-style-cast"
)
add_dependencies(ast_lib gen_syntax_parsers)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(ast_lib PRIVATE trust_pch)
