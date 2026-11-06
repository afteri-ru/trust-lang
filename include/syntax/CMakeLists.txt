# include/syntax/CMakeLists.txt
# Syntax (lexer + parser + macro + term) static library.

set(SYNTAX_GEN_DIR "${CMAKE_BINARY_DIR}/syntax")

trust_component(syntax
    INTERNAL_DIRS grammar lexer macro parser util
    PUBLIC_DIRS term
    PUBLIC_HEADERS
        check_area.hpp lexer.h macro.h module_header.hpp parser.h pragma_macro_x.hpp
        predef_macro.hpp predef_macro_x.hpp parser_services.hpp
    SOURCES
        lexer/lexer.cpp macro/macro.cpp macro/macro_arg_parser.cpp macro/macro_expander.cpp
        macro/macro_matcher.cpp macro/macro_validator.cpp macro/pragma_evaluator.cpp
        macro/pragma_eval.cpp macro/predef_macro.cpp parser/parser.cpp term/term.cpp
        "${SYNTAX_GEN_DIR}/lexer.yy.cpp"
        "${SYNTAX_GEN_DIR}/parser.yy.cpp"
    # build/include (для trust/version.h) и include/ приходят через trust_include_dirs.
    INCLUDE_PRIVATE ${SYNTAX_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
    SYSTEM_INCLUDE_PRIVATE ${LLVM_INCLUDE_DIRS}
    LINK_PUBLIC trust_include_dirs trust_llvm_dirs diag_lib location_lib sourcemap_lib module_api_lib utils_lib
    # Syntax headers (term.h) contain switches over TermID with incomplete coverage.
    # These are used by consumer libraries too, so the suppression must propagate.
    PUBLIC_COMPILE_OPTIONS -Wno-error=switch
    DEPENDS gen_syntax_parsers
)

# -- Mark generated parser files (flex/bison) --
set_source_files_properties(
    "${SYNTAX_GEN_DIR}/lexer.yy.cpp"
    "${SYNTAX_GEN_DIR}/parser.yy.cpp"
    PROPERTIES GENERATED TRUE
)

# Suppress warnings for generated files
set_source_files_properties(
    "${SYNTAX_GEN_DIR}/lexer.yy.cpp"
    "${SYNTAX_GEN_DIR}/parser.yy.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-old-style-cast"
)
