# include/transpiler/CMakeLists.txt
# C++ transpiler library - generates C++ code from AST
# Декомпозиция: драйвер CppTranspiler + компоненты-эмиттеры (Type/Decl/Stmt/Expr/Contract)
# + общий контекст кодогенерации CppEmitContext. Отдельные TU ускоряют сборку.

add_library(transpiler_lib STATIC
    ${PROJECT_SOURCE_DIR}/transpiler/transpiler.cpp
    ${PROJECT_SOURCE_DIR}/transpiler/emit_ctx.cpp
    ${PROJECT_SOURCE_DIR}/transpiler/type_emit.cpp
    ${PROJECT_SOURCE_DIR}/transpiler/decl_emit.cpp
    ${PROJECT_SOURCE_DIR}/transpiler/stmt_emit.cpp
    ${PROJECT_SOURCE_DIR}/transpiler/expr_emit.cpp
    ${PROJECT_SOURCE_DIR}/transpiler/contract_emit.cpp
)
target_link_libraries(transpiler_lib PUBLIC semantic_lib trust_include_dirs)
# transpiler.cpp includes syntax/term.h → parser.h → generated parser.yy.h
target_include_directories(transpiler_lib PRIVATE "${CMAKE_BINARY_DIR}/syntax")
add_dependencies(transpiler_lib gen_syntax_parsers)
