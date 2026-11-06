# include/ast/CMakeLists.txt
# AST library - token definitions, naming, AST node types and the @[...] attribute parser.
# Pure attribute value/registry types (AttrId/Attr/AttrPool/built-in names) live in attrs_lib.

trust_component(ast
    INTERNAL_DIRS convert nodes slots
    PUBLIC_HEADERS
        ast_nodes.hpp binary_op.hpp clone.hpp declaration_ids.hpp ident_name.hpp kind_dispatch.hpp
        ref_syntax.hpp term_to_ast.hpp token_base.hpp token.hpp token_type.hpp trust_prop.hpp
        convert_services.hpp
    SOURCES
        nodes/ident_name.cpp nodes/ast_children.cpp slots/ast_slots.cpp nodes/clone.cpp
        nodes/ast_base.cpp nodes/ast_expr.cpp nodes/ast_decl.cpp nodes/ast_stmt.cpp nodes/token_type.cpp nodes/declaration_ids.cpp
        convert/term_to_ast.cpp convert/term_to_ast_decl.cpp convert/term_to_ast_expr.cpp
    LINK_PUBLIC diag_lib syntax_lib attrs_lib types_lib trust_include_dirs
    LINK_PRIVATE target_lib trust_llvm_dirs
    # syntax/term/term.h includes syntax/parser.h → generated parser.yy.h
    INCLUDE_PRIVATE ${CMAKE_BINARY_DIR}/syntax
    DEPENDS gen_syntax_parsers
)

# term_to_ast.cpp includes syntax headers (term.h → parser.h) that trigger
# -Wswitch / old-style-cast warnings - suppress as errors.
set_source_files_properties(
    ${TRUST_SRC_DIR}/ast/convert/term_to_ast.cpp
    ${TRUST_SRC_DIR}/ast/convert/term_to_ast_decl.cpp
    ${TRUST_SRC_DIR}/ast/convert/term_to_ast_expr.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-error=switch;-Wno-error=old-style-cast"
)

# -- Kind-диспетчеризация: полнота switch по ParserToken::Kind проверяется компилятором. --
# Флаг включён ТОЛЬКО для TU со switch по kind (структурный клон: kind→класс в shallowCopy):
# новый kind без решения ломает сборку. Частичные (предикатные) switch помечены
# TRUST_PARTIAL_ENUM_SWITCH_*.
set_source_files_properties(
    ${TRUST_SRC_DIR}/ast/nodes/clone.cpp
    PROPERTIES COMPILE_OPTIONS "-Wswitch-enum"
)
