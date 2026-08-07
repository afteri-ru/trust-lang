# include/ast/CMakeLists.txt
# AST library — token definitions, naming, AST node types, and attributes
# Attribute subsystem is merged into ast_lib (previously separate attr_lib).

# ── AST library ──
add_library(ast_lib STATIC
    ${PROJECT_SOURCE_DIR}/ast/ident_name.cpp
    ${PROJECT_SOURCE_DIR}/ast/ast_nodes.cpp
    ${PROJECT_SOURCE_DIR}/ast/token_base.cpp
    ${PROJECT_SOURCE_DIR}/ast/token_type.cpp
    ${PROJECT_SOURCE_DIR}/ast/attr_pool.cpp
    ${PROJECT_SOURCE_DIR}/ast/attr_builtin.cpp
    ${PROJECT_SOURCE_DIR}/ast/attr_parser.cpp
    ${PROJECT_SOURCE_DIR}/ast/term_to_ast.cpp
    ${PROJECT_SOURCE_DIR}/ast/lowering.cpp
)
target_link_libraries(ast_lib PUBLIC diag_lib syntax_lib trust_include_dirs)
target_link_libraries(ast_lib PRIVATE trust_llvm_dirs)
# syntax/term.h includes syntax/parser.h → generated parser.yy.h
target_include_directories(ast_lib PRIVATE "${CMAKE_BINARY_DIR}/syntax")
# term_to_ast.cpp includes syntax headers (term.h → parser.h) that trigger
# -Wswitch / old-style-cast warnings — suppress as errors.
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/ast/term_to_ast.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-error=switch;-Wno-error=old-style-cast"
)
add_dependencies(ast_lib gen_syntax_parsers)
