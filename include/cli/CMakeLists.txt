# include/cli/CMakeLists.txt
# CLI — command-line interface for the transpiler pipeline

# ── CLI library ──
add_library(cli_lib STATIC
    ${PROJECT_SOURCE_DIR}/cli/cli.cpp
    ${PROJECT_SOURCE_DIR}/cli/cli_parser.cpp
    cli_parser.hpp
    option_cli_def.hpp
)
target_link_libraries(cli_lib PUBLIC gencpp_lib parser_lib trust_include_dirs)
target_link_libraries(cli_lib PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(cli_lib PRIVATE ${CMAKE_BINARY_DIR}/include)

# ── trust CLI executable ──
add_executable(trust_cli ${PROJECT_SOURCE_DIR}/cli/main.cpp)
target_link_libraries(trust_cli PRIVATE cli_lib trust_include_dirs)
set_target_properties(trust_cli PROPERTIES OUTPUT_NAME trust)

# generate_options_h должен быть собран до trust_cli — заголовочный файл с опциями
# генерируется в include/cli/options.h.in.cmake → ${CMAKE_BINARY_DIR}/include/cli/options.h
add_dependencies(trust_cli generate_options_h)
