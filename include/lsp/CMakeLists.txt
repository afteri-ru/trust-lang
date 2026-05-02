# include/lsp/CMakeLists.txt
# LSP: Trust-lang Language Server Protocol (source mapping)

# ── Library ──
add_library(lsp_lib STATIC
    ${PROJECT_SOURCE_DIR}/lsp/lsp_protocol.cpp
    ${PROJECT_SOURCE_DIR}/lsp/trust_lsp.cpp
    ${PROJECT_SOURCE_DIR}/lsp/transpile.cpp
)

target_link_libraries(lsp_lib PUBLIC
    trust_include_dirs
    debug_lib
)

target_link_libraries(lsp_lib PRIVATE
    nlohmann_json::nlohmann_json
)

# Для доступа к trust/version.h (генерируется в build/include)
target_include_directories(lsp_lib PRIVATE ${CMAKE_BINARY_DIR}/include)

target_include_directories(lsp_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── trust-lsp: LSP server для trust-lang source mapping ──
add_executable(trust-lsp
    ${PROJECT_SOURCE_DIR}/lsp/main.cpp
)

target_link_libraries(trust-lsp PRIVATE
    lsp_lib
    debug_lib
    nlohmann_json::nlohmann_json
)

target_include_directories(trust-lsp PRIVATE
    ${PROJECT_INCLUDE_DIR}
)