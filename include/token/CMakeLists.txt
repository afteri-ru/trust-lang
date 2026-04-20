# include/token/CMakeLists.txt
# Token library — generated token definitions and interfaces
# Тесты управляются из корневого CMakeLists.txt

# Report inherited configuration from parent project
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "[token] Inheriting from parent: compiler=${CMAKE_CXX_COMPILER}, standard=${CMAKE_CXX_STANDARD}, python3=${Python3_EXECUTABLE}")
else()
    # Standalone mode: require own tool checks
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

# ── Пути (используем CMAKE_CURRENT_SOURCE_DIR для работы как subdirectory) ──
set(TOKEN_SRC "${CMAKE_CURRENT_SOURCE_DIR}")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}")

file(MAKE_DIRECTORY "${GEN_DIR}")

# Все генерируемые файлы
set(TOKEN_GEN_OUTPUTS
    "${GEN_DIR}/token.gen.hash"
    "${GEN_DIR}/token.gen.all"
    "${GEN_DIR}/token.gen.name_all"
    "${GEN_DIR}/token.gen.flags_all"
    "${GEN_DIR}/token.gen.lookup_all"
    "${GEN_DIR}/flex.gen.h"
    "${GEN_DIR}/token.gen.def"
    "${GEN_DIR}/parser.gen.y"
)

# ── Генерация токенов ─────────────────────────────────────────────────
add_custom_command(
    OUTPUT ${TOKEN_GEN_OUTPUTS}
    COMMAND Python3::Interpreter "${TOKEN_SRC}/scripts/gen_tokens.py"
        --input  "${TOKEN_SRC}/tokens.def"
        --lock   "${TOKEN_SRC}/scripts/tokens.lock"
        --output-dir "${GEN_DIR}"
        --parser-template "${TOKEN_SRC}/parser.y"
        --lexer "${TOKEN_SRC}/lexer.l"
    DEPENDS
        "${TOKEN_SRC}/tokens.def"
        "${TOKEN_SRC}/scripts/tokens.lock"
        "${TOKEN_SRC}/parser.y"
        "${TOKEN_SRC}/scripts/gen_tokens.py"
    COMMENT "[TokenGen] Генерация токенов из tokens.def..."
    VERBATIM
)

add_custom_target(token_gen_target ALL DEPENDS ${TOKEN_GEN_OUTPUTS})

# ── INTERFACE библиотека (заголовки) ─────────────────────────────────
add_library(token_lib INTERFACE)
target_include_directories(token_lib INTERFACE
    "${TOKEN_SRC}/.."
    "${GEN_DIR}/.."
)
target_link_libraries(token_lib INTERFACE diag_lib)
add_dependencies(token_lib token_gen_target)