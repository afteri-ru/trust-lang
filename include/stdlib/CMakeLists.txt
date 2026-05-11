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

# ── stdlib executable ──
set(STDLIB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/stdlib_out")

add_executable(stdlib ${PROJECT_SOURCE_DIR}/stdlib/stdlib_main.cpp)
target_link_libraries(stdlib PRIVATE stdlib_lib)

target_compile_definitions(stdlib PRIVATE
    SOURCE_FILE_PATH="${PROJECT_SOURCE_DIR}/stdlib/trust_stdlib.cpp"
    PROJECT_INCLUDE_DIR="${PROJECT_INCLUDE_DIR}"
)

# ── Stdlib generation ──
set(STDLIB_TIMESTAMP "${STDLIB_OUTPUT_DIR}/.build_timestamp")

add_custom_command(
    OUTPUT ${STDLIB_TIMESTAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STDLIB_OUTPUT_DIR}"
    COMMAND $<TARGET_FILE:stdlib> "${STDLIB_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${STDLIB_TIMESTAMP}"
    DEPENDS stdlib ${PROJECT_SOURCE_DIR}/stdlib/trust_stdlib.cpp
    COMMENT "Running stdlib AST analysis -> ${STDLIB_OUTPUT_DIR}"
    VERBATIM
)

add_custom_target(stdlib_run DEPENDS ${STDLIB_TIMESTAMP})
