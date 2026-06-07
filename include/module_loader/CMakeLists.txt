# include/module_loader/CMakeLists.txt
# Module loader library — needed by diag::Context (and pipeline_lib)

set(SYNTAX_GEN_DIR "${CMAKE_BINARY_DIR}/syntax")

add_library(module_loader_lib STATIC
    ${PROJECT_SOURCE_DIR}/pipeline/module_loader.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/module_registry.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/term_to_ast.cpp
)
target_link_libraries(module_loader_lib PUBLIC trust_include_dirs ast_lib)
target_link_libraries(module_loader_lib PRIVATE syntax_lib)
target_include_directories(module_loader_lib PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${SYNTAX_GEN_DIR}
)
# term_to_ast.cpp and module_loader.cpp include syntax headers (macro.h → term.h)
# that trigger -Wswitch/old-style-cast
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/pipeline/term_to_ast.cpp
    ${PROJECT_SOURCE_DIR}/pipeline/module_loader.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-error=switch;-Wno-error=old-style-cast"
)
add_dependencies(module_loader_lib generate_makefile_header gen_syntax_parsers)
