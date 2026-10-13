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

# Бинарники должны быть собраны до запуска теста.
add_dependencies(integration trust-playground trust-lsp)

# Отдельный каталог в build-дереве для интеграционного теста: и бинарник, и его
# временный конфиг (tmp-dir = $<TARGET_FILE_DIR:integration>) лежат здесь, а не в
# корне _build. См. test/integration/playground_main.cpp (tmp_dir + "/trust_pg_int.conf").
set_target_properties(integration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

trust_add_test_suite(integration_tests
    TIMEOUT 120
    FIXTURES_REQUIRED integration_deps
    RUN_SERIAL
    COMMAND $<TARGET_FILE:integration>
            $<TARGET_FILE:trust-playground>
            $<TARGET_FILE:trust-lsp>
            ${CMAKE_SOURCE_DIR}/examples
            $<TARGET_FILE_DIR:integration>
)
