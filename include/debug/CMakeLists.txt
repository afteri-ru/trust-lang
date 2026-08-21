# include/debug/CMakeLists.txt
# Debug: Trust-lang debug system (source mapping + GDB/MI bridge)

# -- debug_lib (no LLDB dependency) --
add_library(debug_lib STATIC
    ${PROJECT_SOURCE_DIR}/debug/gdb_debug.cpp
    ${PROJECT_SOURCE_DIR}/debug/dap_transport.cpp
    ${PROJECT_SOURCE_DIR}/debug/dap_handler.cpp
)

target_link_libraries(debug_lib PUBLIC
    trust_include_dirs
)

target_link_libraries(debug_lib PUBLIC
    diag_lib
)
target_link_libraries(debug_lib PRIVATE
    msgpack-c-static
)

# Для доступа к trust/version.h (генерируется в build/include)
target_include_directories(debug_lib PRIVATE ${CMAKE_BINARY_DIR}/include)

target_include_directories(debug_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# -- trust-dap: DAP server для trust-lang debugging --
add_executable(trust-dap
    ${PROJECT_SOURCE_DIR}/debug/trust_dap.cpp
)

target_link_libraries(trust-dap PRIVATE
    debug_lib
    nlohmann_json::nlohmann_json
)

target_include_directories(trust-dap PRIVATE
    ${CMAKE_BINARY_DIR}/include
    ${PROJECT_INCLUDE_DIR}
)
