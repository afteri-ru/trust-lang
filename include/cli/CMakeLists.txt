# include/cli/CMakeLists.txt
# CLI — command-line interface for the transpiler pipeline

# ── CLI library ──
add_library(cli_lib STATIC
    cli.cpp
)
target_link_libraries(cli_lib PUBLIC gencpp_lib diag_lib)
target_link_trust_includes(cli_lib)

# ── trust CLI executable ──
add_executable(trust_cli main.cpp)
target_link_libraries(trust_cli PRIVATE cli_lib)
target_link_trust_includes(trust_cli)
set_target_properties(trust_cli PROPERTIES OUTPUT_NAME trust)