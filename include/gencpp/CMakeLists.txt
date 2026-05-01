# include/gencpp/CMakeLists.txt
# gencpp: C++ code generation library and tests

# ── gencpp: Library ──
add_library(gencpp_lib STATIC
    ${PROJECT_SOURCE_DIR}/src/gencpp/ast.cpp
    ${PROJECT_SOURCE_DIR}/src/gencpp/ast_printer.cpp
    ${PROJECT_SOURCE_DIR}/src/gencpp/cpp_generator.cpp
    ${PROJECT_SOURCE_DIR}/src/gencpp/semantic_analyzer.cpp
)
target_link_libraries(gencpp_lib PUBLIC parser_lib types_lib trust_include_dirs)
add_dependencies(gencpp_lib parser_lib)

# ── gencpp: Test AST format parser library (test-only) ──
# DISABLED: ast_format_parser.cpp uses std::expected API incompatible with clang-22
add_library(gencpp_test_lib STATIC
    ${PROJECT_SOURCE_DIR}/tests/gencpp/ast_format_parser.cpp
)
target_include_directories(gencpp_test_lib PUBLIC ${PROJECT_SOURCE_DIR}/tests/gencpp)
target_link_libraries(gencpp_test_lib PUBLIC gencpp_lib trust_include_dirs)

# ── gencpp: CLI tool for transpiling from AST files ──
add_executable(gencpp_from_ast ${PROJECT_SOURCE_DIR}/tests/gencpp/gencpp_from_ast.cpp)
target_trust_module(gencpp_from_ast)
target_link_libraries(gencpp_from_ast PRIVATE gencpp_lib gencpp_test_lib)


