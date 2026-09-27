# test/pcp/CMakeLists.txt
# Интеграционный тест предкомпилированного пакета (.pcp): создание пакета режимом `trust -m`
# и эквивалентность Trust-интерфейса (%<native>) реальным символам в упакованном .o.
# Запускается после basic-наборов (порядок CTest: unit → lit → vscode → examples → integration → pm → pcp).

add_executable(pcp_integration
    ${CMAKE_CURRENT_SOURCE_DIR}/integration.cpp
)

target_link_libraries(pcp_integration PRIVATE packages_lib)
target_include_directories(pcp_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(pcp_integration PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(pcp_integration trust)

add_test(NAME pcp_symbol_tests
    COMMAND $<TARGET_FILE:pcp_integration>
            $<TARGET_FILE:trust>
            $<TARGET_FILE_DIR:pcp_integration>
)
set_tests_properties(pcp_symbol_tests PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
)
