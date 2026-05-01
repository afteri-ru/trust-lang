# include/dap/CMakeLists.txt
# DAP: Debug Adapter Protocol server for Trust language

# ── DAP library ──
add_library(dap_lib STATIC
    ${PROJECT_SOURCE_DIR}/src/dap/dap_protocol.cpp
    ${PROJECT_SOURCE_DIR}/src/dap/gdb_bridge.cpp
    ${PROJECT_SOURCE_DIR}/src/dap/trust_mapper.cpp
)
target_link_libraries(dap_lib PUBLIC nlohmann_json::nlohmann_json trust_include_dirs)
target_include_directories(dap_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ── DAP executable ──
add_executable(trust_dap ${PROJECT_SOURCE_DIR}/src/dap/main.cpp)
target_link_libraries(trust_dap PRIVATE dap_lib trust_include_dirs)
set_target_properties(trust_dap PROPERTIES OUTPUT_NAME trust-dap)
