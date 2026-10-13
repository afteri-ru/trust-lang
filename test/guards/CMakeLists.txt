# test/guards/CMakeLists.txt
# Link-level guard: syntax + ast + session НЕ должны зависеть от module_loader_lib.
# Цель линкует только нижние слои; если syntax/ast/session начнут обращаться к символам
# ModuleLoader, линковка упадёт (нет module_loader_lib в зависимостях).

add_executable(no_module_loader_guard guard_main.cpp)
target_link_libraries(no_module_loader_guard PRIVATE trust_include_dirs syntax_lib ast_lib session_lib)
target_include_directories(no_module_loader_guard PRIVATE "${CMAKE_BINARY_DIR}/syntax")
add_dependencies(no_module_loader_guard gen_syntax_parsers)
target_compile_options(no_module_loader_guard PRIVATE -Wno-error=switch)
# Регистрация через общий helper: TIMEOUT обязателен (см. cmake/test.cmake).
trust_add_test_suite(no_module_loader_guard
    TIMEOUT 120
    LABELS guards
    COMMAND $<TARGET_FILE:no_module_loader_guard>
)
