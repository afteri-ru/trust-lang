# cmake/generate_parsers.cmake
# Generate bison parser and flex lexer from include/syntax/ sources.
# Секция %token парсера генерируется из единого источника терминалов (term_types.h:
# TERMS с маркером T + SYMBOL_TOKENS) на этапе configure — Bison не раскрывает X-макросы
# в директивах %token, поэтому %token-блок собирается здесь и вставляется в parser.y.in.

set(SYNTAX_LIB_DIR "${CMAKE_SOURCE_DIR}/include/syntax")
set(SYNTAX_GEN_DIR "${CMAKE_BINARY_DIR}/syntax")

# ── Ensure output directory exists ──
file(MAKE_DIRECTORY "${SYNTAX_GEN_DIR}")

# ── Генерация parser.y из parser.y.in (замена @@TOKENS@@ на сгенерированные %token) ──
# Источник имён токенов — TERMS (записи с маркером T) и SYMBOL_TOKENS (все). END объявляется
# отдельно как %token END 0 (специальный EOF-код). Текстовые описания токенов не генерируются.
function(gen_tokens)
    file(READ "${SYNTAX_LIB_DIR}/term_types.h" _content)
    set(_tokens "%token END 0\n")

    string(FIND "${_content}" "#define TERMS(" _p1)
    string(FIND "${_content}" "#define SYMBOL_TOKENS(" _p2)
    math(EXPR _len1 "${_p2} - ${_p1}")

    # ── TERMS: терминалы помечены маркером ", T)" ──
    string(SUBSTRING "${_content}" "${_p1}" "${_len1}" _terms_part)
    string(REPLACE ", T)" ", TT_" _terms_part "${_terms_part}")
    string(REPLACE "_(" "__T_" _terms_part "${_terms_part}")
    string(REPLACE ")" "__R_" _terms_part "${_terms_part}")
    string(REGEX MATCHALL "__T_[A-Za-z_][A-Za-z0-9_]*, [A-Za-z_][A-Za-z0-9_]*, TT_" _entries "${_terms_part}")
    foreach(_e IN LISTS _entries)
        string(REGEX MATCH "__T_([A-Za-z_][A-Za-z0-9_]*)" _dummy "${_e}")
        set(_name "${CMAKE_MATCH_1}")
        string(APPEND _tokens "%token ${_name}\n")
    endforeach()

    # ── SYMBOL_TOKENS: все записи — терминалы ──
    string(SUBSTRING "${_content}" "${_p2}" -1 _symbol_part)
    string(REPLACE "_(" "__T_" _symbol_part "${_symbol_part}")
    string(REGEX MATCHALL "__T_[A-Za-z_][A-Za-z0-9_]*" _entries2 "${_symbol_part}")
    foreach(_e IN LISTS _entries2)
        string(REGEX MATCH "__T_([A-Za-z_][A-Za-z0-9_]*)" _dummy "${_e}")
        set(_name "${CMAKE_MATCH_1}")
        string(APPEND _tokens "%token ${_name}\n")
    endforeach()

    file(READ "${SYNTAX_LIB_DIR}/parser.y.in" _yin)
    string(REPLACE "@@TOKENS@@" "${_tokens}" _yout "${_yin}")
    file(WRITE "${SYNTAX_GEN_DIR}/parser.y" "${_yout}")
endfunction()

gen_tokens()

# ── Bison parser generation ──
set(BISON_PARSER_Y "${SYNTAX_GEN_DIR}/parser.y")
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

# ── Перезапуск configure при изменении источника токенов/шаблона ──
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SYNTAX_LIB_DIR}/term_types.h"
    "${SYNTAX_LIB_DIR}/parser.y.in"
    "${CMAKE_CURRENT_LIST_FILE}"
)