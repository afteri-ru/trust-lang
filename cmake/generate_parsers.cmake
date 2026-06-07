# cmake/generate_parsers.cmake
# Generate bison parser and flex lexer from include/syntax/ sources

set(SYNTAX_LIB_DIR "${CMAKE_SOURCE_DIR}/include/syntax")
set(SYNTAX_GEN_DIR "${CMAKE_BINARY_DIR}/syntax")

# ── Ensure output directory exists ──
file(MAKE_DIRECTORY "${SYNTAX_GEN_DIR}")

# ── Bison parser generation ──
set(BISON_PARSER_Y "${SYNTAX_LIB_DIR}/parser.y")
set(BISON_PARSER_CPP "${SYNTAX_GEN_DIR}/parser.yy.cpp")
set(BISON_PARSER_H "${SYNTAX_GEN_DIR}/parser.yy.h")

add_custom_command(
    OUTPUT "${BISON_PARSER_CPP}" "${BISON_PARSER_H}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SYNTAX_GEN_DIR}"
    COMMAND ${BISON_EXECUTABLE}
        ARGS --output-file="${BISON_PARSER_CPP}"
             --defines="${BISON_PARSER_H}"
             --warnings=all
             --verbose
             --debug
             "${BISON_PARSER_Y}"
    DEPENDS "${BISON_PARSER_Y}"
    COMMENT "Generating bison parser: parser.yy.cpp + parser.yy.h"
)

# ── Flex lexer generation ──
set(FLEX_LEXER_L "${SYNTAX_LIB_DIR}/lexer.l")
set(FLEX_LEXER_CPP "${SYNTAX_GEN_DIR}/lexer.yy.cpp")
set(FLEX_LEXER_H "${SYNTAX_GEN_DIR}/lexer.yy.h")

add_custom_command(
    OUTPUT "${FLEX_LEXER_CPP}" "${FLEX_LEXER_H}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SYNTAX_GEN_DIR}"
    COMMAND ${FLEX_EXECUTABLE}
        ARGS --outfile="${FLEX_LEXER_CPP}"
             --header-file="${FLEX_LEXER_H}"
             --noline
             "${FLEX_LEXER_L}"
    DEPENDS "${FLEX_LEXER_L}"
    COMMENT "Generating flex lexer: lexer.yy.cpp + lexer.yy.h"
)

# Convenience target to regenerate both parsers
add_custom_target(gen_syntax_parsers
    DEPENDS "${BISON_PARSER_CPP}" "${FLEX_LEXER_CPP}"
    COMMENT "Regenerate bison parser and flex lexer from include/syntax/"
)