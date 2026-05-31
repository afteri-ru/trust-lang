# include/ast/CMakeLists.txt
# AST library — token definitions, naming, and AST node types
# This component has no dependency on flex/bison/macros.

set(GEN_DIR "${CMAKE_BINARY_DIR}/include/ast")

# ── Token generation ──
set(TOKEN_GEN_OUTPUTS
    "${GEN_DIR}/token.gen.hash"
    "${GEN_DIR}/token.gen.all"
    "${GEN_DIR}/token.gen.name_all"
    "${GEN_DIR}/token.gen.flags_all"
    "${GEN_DIR}/token.gen.lookup_all"
    "${GEN_DIR}/token.gen.def"
    "${GEN_DIR}/parser.gen.y"
)

add_custom_command(
    OUTPUT ${TOKEN_GEN_OUTPUTS}
    COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_tokens.py"
        --input  "${CMAKE_CURRENT_SOURCE_DIR}/tokens.def"
        --lock   "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tokens.lock"
        --output-dir "${GEN_DIR}"
        --parser-template "${CMAKE_SOURCE_DIR}/include/parser/parser.y"
        --lexer "${CMAKE_SOURCE_DIR}/include/parser/lexer.l"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tokens.def"
        "${CMAKE_SOURCE_DIR}/include/parser/parser.y"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_tokens.py"
    COMMENT "[TokenGen] Generating tokens from tokens.def..."
    VERBATIM
)

# ── Generated files target ──
add_custom_target(ast_generated ALL
    DEPENDS ${TOKEN_GEN_OUTPUTS}
)

# ── AST library ──
add_library(ast_lib STATIC
    ${PROJECT_SOURCE_DIR}/ast/naming.cpp
    ${PROJECT_SOURCE_DIR}/ast/attr_pool.cpp
    ${PROJECT_SOURCE_DIR}/ast/attr_builtin.cpp
    ${PROJECT_SOURCE_DIR}/ast/attr_parser.cpp
)
target_link_libraries(ast_lib PUBLIC diag_lib)
target_include_directories(ast_lib PUBLIC
    ${PROJECT_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/include
)
target_include_directories(ast_lib SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
add_dependencies(ast_lib ast_generated)
