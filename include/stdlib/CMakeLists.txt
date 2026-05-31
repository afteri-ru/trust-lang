# include/stdlib/CMakeLists.txt
# Stdlib library + executable — AST-analysis via clang-matchers

# ── stdlib_lib ──
add_library(stdlib_lib STATIC
    ${PROJECT_SOURCE_DIR}/stdlib/analyzer.cpp
    ${PROJECT_SOURCE_DIR}/stdlib/api_comparator.cpp
    ${PROJECT_SOURCE_DIR}/stdlib/formatter.cpp
    ${PROJECT_SOURCE_DIR}/stdlib/ast_matcher.cpp
    ${PROJECT_SOURCE_DIR}/stdlib/name_utils.cpp
    ${PROJECT_SOURCE_DIR}/stdlib/trust_stdlib.cpp
    ${PROJECT_INCLUDE_DIR}/stdlib/analyzer.hpp
    ${PROJECT_INCLUDE_DIR}/stdlib/api_comparator.hpp
    ${PROJECT_INCLUDE_DIR}/stdlib/formatter.hpp
    ${PROJECT_INCLUDE_DIR}/stdlib/ast_matcher.hpp
    ${PROJECT_INCLUDE_DIR}/stdlib/patterns.hpp
    ${PROJECT_INCLUDE_DIR}/stdlib/name_utils.hpp
)

target_compile_definitions(stdlib_lib PRIVATE ${LLVM_DEFINITIONS})

target_include_directories(stdlib_lib PUBLIC ${PROJECT_INCLUDE_DIR})
target_include_directories(stdlib_lib SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})

target_link_libraries(stdlib_lib PUBLIC clang-cpp LLVM)

