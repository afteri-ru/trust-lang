# test/integration/CMakeLists.txt
# Интеграционный тест полной цепочки playground (server→worker→lsp→worker→server).
# Добавлен ПОСЛЕ test/examples и запускается только после него через фикстуру
# integration_deps (которую setup'ит examples_tests). Запускает реальные
# бинарники trust-playground/trust-lsp со временным конфигом - без установки и без
# системных настроек (просто запуск приложения с контролем работы).

add_executable(integration
    ${CMAKE_CURRENT_SOURCE_DIR}/playground_main.cpp
)

target_link_libraries(integration PRIVATE
    playground_lib
    nlohmann_json::nlohmann_json
)

target_include_directories(integration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Бинарники должны быть собраны до запуска теста.
add_dependencies(integration trust-playground trust-lsp)

add_test(NAME integration_tests
    COMMAND $<TARGET_FILE:integration>
            $<TARGET_FILE:trust-playground>
            $<TARGET_FILE:trust-lsp>
            ${CMAKE_SOURCE_DIR}/examples
            $<TARGET_FILE_DIR:integration>
)
set_tests_properties(integration_tests PROPERTIES
    FIXTURES_REQUIRED integration_deps
    RUN_SERIAL TRUE
    TIMEOUT 120
)
