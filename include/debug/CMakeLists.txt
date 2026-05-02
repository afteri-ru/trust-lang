# include/debug/CMakeLists.txt
# Debug: Trust-lang debug system (source mapping + LLDB bridge)

# ── Library ──
add_library(debug_lib STATIC
    ${PROJECT_SOURCE_DIR}/debug/trust_debug.cpp
    ${PROJECT_SOURCE_DIR}/debug/trust_source.cpp
    ${PROJECT_SOURCE_DIR}/debug/trust_msgpack.cpp
    ${PROJECT_SOURCE_DIR}/debug/dap_transport.cpp
)

target_link_libraries(debug_lib PUBLIC
    trust_include_dirs
)

target_link_libraries(debug_lib PRIVATE
    ${LLDB_LIBRARIES}
    msgpack-c-static
)

# Для доступа к trust/version.h (генерируется в build/include)
target_include_directories(debug_lib PRIVATE ${CMAKE_BINARY_DIR}/include)

target_include_directories(debug_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LLDB_INCLUDE_DIRS}
)

# ── trust-dap: DAP server для trust-lang debugging ──
add_executable(trust-dap
    ${PROJECT_SOURCE_DIR}/debug/trust_dap.cpp
)

target_link_libraries(trust-dap PRIVATE
    debug_lib
    nlohmann_json::nlohmann_json
)

target_include_directories(trust-dap PRIVATE
    ${PROJECT_INCLUDE_DIR}
)
