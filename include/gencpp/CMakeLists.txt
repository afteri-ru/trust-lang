# include/gencpp/CMakeLists.txt
# gencpp: C++ code generation library and tests

# ── gencpp: Library ──
add_library(gencpp_lib STATIC
    ${CMAKE_SOURCE_DIR}/src/gencpp/ast.cpp
    ${CMAKE_SOURCE_DIR}/src/gencpp/ast_printer.cpp
    ${CMAKE_SOURCE_DIR}/src/gencpp/cpp_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/gencpp/semantic_analyzer.cpp
)
target_link_libraries(gencpp_lib PUBLIC parser_lib diag_lib types_lib)
add_dependencies(gencpp_lib parser_lib_target)
target_link_trust_includes(gencpp_lib)

# ── gencpp: Test AST format parser library (test-only) ──
add_library(gencpp_test_lib STATIC
    ${CMAKE_SOURCE_DIR}/tests/gencpp/ast_format_parser.cpp
)
target_include_directories(gencpp_test_lib PUBLIC ${CMAKE_SOURCE_DIR}/tests/gencpp)
target_link_libraries(gencpp_test_lib PUBLIC gencpp_lib)
target_link_trust_includes(gencpp_test_lib)

# ── gencpp: CLI tool for transpiling from AST files ──
add_executable(gencpp_from_ast ${CMAKE_SOURCE_DIR}/tests/gencpp/gencpp_from_ast.cpp)
target_trust_module(gencpp_from_ast)
target_link_libraries(gencpp_from_ast PRIVATE gencpp_lib gencpp_test_lib)

# ── gencpp: Unit tests ──
add_executable(gencpp_unit_tests
    ${CMAKE_SOURCE_DIR}/unittest/gencpp/ast_nodes_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/gencpp/ast_parsing_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/gencpp/ast_printing_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/gencpp/cpp_generator_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/gencpp/gencpp_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/gencpp/loop_control_test.cpp
    ${CMAKE_SOURCE_DIR}/unittest/gencpp/semantic_analyzer_test.cpp
)
target_trust_module(gencpp_unit_tests)
target_link_libraries(gencpp_unit_tests PRIVATE gencpp_lib gencpp_test_lib GTest::gtest GTest::gtest_main)
target_include_directories(gencpp_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/unittest/gencpp ${CMAKE_SOURCE_DIR}/tests/gencpp)
target_link_trust_includes(gencpp_unit_tests)

add_custom_target(run_gencpp_unittest
    COMMAND $<TARGET_FILE:gencpp_unit_tests>
    DEPENDS gencpp_unit_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running gencpp unit tests..."
    USES_TERMINAL
)


