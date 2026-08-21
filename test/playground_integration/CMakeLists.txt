# test/playground_integration/CMakeLists.txt
# Интеграционный тест полной цепочки playground (server→worker→lsp→worker→server).
# Добавлен ПОСЛЕ test/examples и запускается только после него через фикстуру
# playground_integration_deps (которую setup'ит examples_tests). Запускает реальные
# бинарники trust-playground/trust-lsp со временным конфигом - без установки и без
# системных настроек (просто запуск приложения с контролем работы).

add_executable(playground_integration
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(playground_integration PRIVATE
    playground_lib
    nlohmann_json::nlohmann_json
)

target_include_directories(playground_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Бинарники должны быть собраны до запуска теста.
add_dependencies(playground_integration trust-playground trust-lsp)

add_test(NAME playground_integration_tests
    COMMAND $<TARGET_FILE:playground_integration>
            $<TARGET_FILE:trust-playground>
            $<TARGET_FILE:trust-lsp>
            ${CMAKE_SOURCE_DIR}/examples
            $<TARGET_FILE_DIR:playground_integration>
)
set_tests_properties(playground_integration_tests PROPERTIES
    FIXTURES_REQUIRED playground_integration_deps
    RUN_SERIAL TRUE
    TIMEOUT 120
)
