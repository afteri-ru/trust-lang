# include/parser/CMakeLists.txt
# Parser library — unified token definitions and interfaces

set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# ── Flex: generate lexer from lexer.l ──
set(LEXER_L "${CMAKE_CURRENT_SOURCE_DIR}/lexer.l")

add_custom_command(
    OUTPUT "${GEN_DIR}/lex.yy.cpp" "${GEN_DIR}/flex.gen.h"
    COMMAND ${FLEX_EXECUTABLE} -w
        --outfile=${GEN_DIR}/lex.yy.cpp
        --header-file=${GEN_DIR}/flex.gen.h
        ${LEXER_L}
    DEPENDS "${LEXER_L}"
    COMMENT "[Flex] Generating lexer from lexer.l..."
    VERBATIM
)

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
        --parser-template "${CMAKE_CURRENT_SOURCE_DIR}/parser.y"
        --lexer "${CMAKE_CURRENT_SOURCE_DIR}/lexer.l"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tokens.def"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tokens.lock"
        "${CMAKE_CURRENT_SOURCE_DIR}/parser.y"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_tokens.py"
        "${GEN_DIR}/lex.yy.cpp"
        "${GEN_DIR}/flex.gen.h"
    COMMENT "[TokenGen] Generating tokens from tokens.def..."
    VERBATIM
)

add_custom_target(parser_lib_target ALL
    DEPENDS
        "${GEN_DIR}/lex.yy.cpp"
        "${GEN_DIR}/flex.gen.h"
        ${TOKEN_GEN_OUTPUTS}
)

# ── Parser library ──
add_library(parser_lib STATIC
    ${PROJECT_SOURCE_DIR}/src/parser/mmproc.cpp
    ${GEN_DIR}/lex.yy.cpp
)
target_link_libraries(parser_lib PUBLIC diag_lib)
target_include_directories(parser_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_BINARY_DIR}/..
)
add_dependencies(parser_lib parser_lib_target)

