# include/parser/CMakeLists.txt
# Parser library — unified token definitions and interfaces (migrated from token)

# Report inherited configuration from parent project
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "[parser] Inheriting from parent: compiler=${CMAKE_CXX_COMPILER}, standard=${CMAKE_CXX_STANDARD}, python3=${Python3_EXECUTABLE}")
else()
    # Standalone mode: require own tool checks
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

# ── Пути (используем CMAKE_CURRENT_SOURCE_DIR для работы как subdirectory) ──
set(PARSER_SRC "${CMAKE_CURRENT_SOURCE_DIR}")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}")

file(MAKE_DIRECTORY "${GEN_DIR}")

# ── Flex: генерация lexer из lexer.l ─────────────────────────────────
set(LEXER_L "${PARSER_SRC}/lexer.l")
set(LEXER_GEN_OUTPUTS
    "${GEN_DIR}/lex.yy.cpp"
    "${GEN_DIR}/flex.gen.h"
)

# Flex генерирует в GEN_DIR (уже создана выше через file(MAKE_DIRECTORY))
add_custom_command(
    OUTPUT ${LEXER_GEN_OUTPUTS}
    COMMAND sh -c "${FLEX_EXECUTABLE} -w --outfile=${GEN_DIR}/lex.yy.cpp --header-file=${GEN_DIR}/flex.gen.h ${LEXER_L}"
    DEPENDS "${LEXER_L}"
    COMMENT "[Flex] Generating lexer from lexer.l..."
    VERBATIM
)

# Все генерируемые токенами файлы
set(TOKEN_GEN_OUTPUTS
    "${GEN_DIR}/token.gen.hash"
    "${GEN_DIR}/token.gen.all"
    "${GEN_DIR}/token.gen.name_all"
    "${GEN_DIR}/token.gen.flags_all"
    "${GEN_DIR}/token.gen.lookup_all"
    "${GEN_DIR}/token.gen.def"
    "${GEN_DIR}/parser.gen.y"
)

# ── Генерация токенов ─────────────────────────────────────────────────
add_custom_command(
    OUTPUT ${TOKEN_GEN_OUTPUTS}
    COMMAND Python3::Interpreter "${PARSER_SRC}/scripts/gen_tokens.py"
        --input  "${PARSER_SRC}/tokens.def"
        --lock   "${PARSER_SRC}/scripts/tokens.lock"
        --output-dir "${GEN_DIR}"
        --parser-template "${PARSER_SRC}/parser.y"
        --lexer "${PARSER_SRC}/lexer.l"
    DEPENDS
        "${PARSER_SRC}/tokens.def"
        "${PARSER_SRC}/scripts/tokens.lock"
        "${PARSER_SRC}/parser.y"
        "${PARSER_SRC}/scripts/gen_tokens.py"
        ${LEXER_GEN_OUTPUTS}
    COMMENT "[TokenGen] Генерация токенов из tokens.def..."
    VERBATIM
)

set(ALL_PARSER_GEN_OUTPUTS
    ${LEXER_GEN_OUTPUTS}
    ${TOKEN_GEN_OUTPUTS}
)

add_custom_target(parser_lib_target ALL DEPENDS ${ALL_PARSER_GEN_OUTPUTS})

# ── Parser library (заголовки + mmproc + lexer) ─────────────────────────────────
add_library(parser_lib STATIC
    ${CMAKE_SOURCE_DIR}/src/parser/mmproc.cpp
    ${GEN_DIR}/lex.yy.cpp
)
target_include_directories(parser_lib PUBLIC
    "${PARSER_SRC}/.."
    "${GEN_DIR}/.."
)
target_link_libraries(parser_lib PUBLIC diag_lib)
add_dependencies(parser_lib parser_lib_target)
target_link_trust_includes(parser_lib)

# ── Parser unit tests ──
add_executable(parser_unit_tests
    ${CMAKE_SOURCE_DIR}/unittest/parser/token_gen_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/parser/lexer_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/parser/lexer_integration_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/parser/mmproc_test.cpp
    ${GEN_DIR}/lex.yy.cpp
)
target_link_libraries(parser_unit_tests PRIVATE parser_lib gencpp_lib GTest::gtest GTest::gtest_main)
target_include_directories(parser_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/unittest/parser)
target_link_trust_includes(parser_unit_tests)

add_custom_target(run_parser_unittest
    COMMAND $<TARGET_FILE:parser_unit_tests>
    DEPENDS parser_unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running parser unit tests..."
    USES_TERMINAL
)