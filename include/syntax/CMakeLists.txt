# include/syntax/CMakeLists.txt
# Syntax (lexer + parser) static library

set(SYNTAX_GEN_DIR "${CMAKE_BINARY_DIR}/syntax")

# -- Mark generated parser files (flex/bison) --
set_source_files_properties(
    "${SYNTAX_GEN_DIR}/lexer.yy.cpp"
    "${SYNTAX_GEN_DIR}/parser.yy.cpp"
    PROPERTIES GENERATED TRUE
)

# -- Syntax library --
add_library(syntax_lib STATIC
    ${PROJECT_SOURCE_DIR}/syntax/lexer.cpp
    ${PROJECT_SOURCE_DIR}/syntax/macro.cpp
    ${PROJECT_SOURCE_DIR}/syntax/parser.cpp
    ${PROJECT_SOURCE_DIR}/syntax/term.cpp
    ${PROJECT_SOURCE_DIR}/syntax/types.cpp
    "${SYNTAX_GEN_DIR}/lexer.yy.cpp"
    "${SYNTAX_GEN_DIR}/parser.yy.cpp"
)

# -- Ensure parsers are generated before building library --
add_dependencies(syntax_lib gen_syntax_parsers)

# -- Include paths --
target_include_directories(syntax_lib PUBLIC
    ${PROJECT_INCLUDE_DIR}
)
target_include_directories(syntax_lib PRIVATE
    ${SYNTAX_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/include
)
target_include_directories(syntax_lib SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
)

# -- Link dependencies --
target_link_libraries(syntax_lib PUBLIC
    trust_include_dirs
    trust_llvm_dirs
)

# Syntax headers (term.h) contain switches over TermID with incomplete coverage.
# These are used by consumer libraries too, so the suppression must propagate.
target_compile_options(syntax_lib PUBLIC
    -Wno-error=switch
)

# Suppress warnings for generated files
set_source_files_properties(
    "${SYNTAX_GEN_DIR}/lexer.yy.cpp"
    "${SYNTAX_GEN_DIR}/parser.yy.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-old-style-cast"
)