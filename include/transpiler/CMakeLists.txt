# include/transpiler/CMakeLists.txt
# C++ transpiler library — generates C++ code from AST

add_library(transpiler_lib STATIC
    ${PROJECT_SOURCE_DIR}/transpiler/transpiler.cpp
)
target_link_libraries(transpiler_lib PUBLIC semantic_lib trust_include_dirs)
# transpiler.cpp includes syntax/term.h → parser.h → generated parser.yy.h
target_include_directories(transpiler_lib PRIVATE "${CMAKE_BINARY_DIR}/syntax")
add_dependencies(transpiler_lib gen_syntax_parsers)
