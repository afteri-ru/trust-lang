# include/transpiler/CMakeLists.txt
# C++ transpiler library — generates C++ code from AST

add_library(transpiler_lib STATIC
    ${PROJECT_SOURCE_DIR}/transpiler/transpiler.cpp
)
target_link_libraries(transpiler_lib PUBLIC semantic_lib trust_include_dirs)
