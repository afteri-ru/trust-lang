# include/cli/CMakeLists.txt
# CLI — command-line interface for the transpiler pipeline

# ── CLI library ──
add_library(cli_lib STATIC
    ${CMAKE_SOURCE_DIR}/src/cli/cli.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/cli_parser.cpp
    cli_parser.hpp
    option_cli_def.hpp
)
target_link_libraries(cli_lib PUBLIC gencpp_lib diag_lib parser_lib)
target_link_trust_includes(cli_lib)
target_include_directories(cli_lib PRIVATE ${CMAKE_BINARY_DIR}/include)

# ── trust CLI executable ──
add_executable(trust_cli ${CMAKE_SOURCE_DIR}/src/cli/main.cpp)
target_link_libraries(trust_cli PRIVATE cli_lib)
target_link_trust_includes(trust_cli)
set_target_properties(trust_cli PROPERTIES OUTPUT_NAME trust)