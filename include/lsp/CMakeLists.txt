# include/lsp/CMakeLists.txt
# LSP: Trust-lang Language Server Protocol (source mapping)

# -- Library --
add_library(lsp_lib STATIC
    ${PROJECT_SOURCE_DIR}/lsp/lsp_protocol.cpp
    ${PROJECT_SOURCE_DIR}/lsp/trust_lsp.cpp
    ${PROJECT_SOURCE_DIR}/lsp/html_emit.cpp
    ${PROJECT_SOURCE_DIR}/lsp/builtin_catalog.cpp
    ${PROJECT_SOURCE_DIR}/lsp/completion.cpp
)

target_link_libraries(lsp_lib PUBLIC
    trust_include_dirs
    debug_lib
    semantic_lib
    transpiler_lib
    pipeline_lib
)

target_link_libraries(lsp_lib PRIVATE
    nlohmann_json::nlohmann_json
)

# Для доступа к trust/version.h (генерируется в _build/include)
target_include_directories(lsp_lib PRIVATE ${CMAKE_BINARY_DIR}/include)
# Для доступа к макропроцессору (syntax/macro.h) и реестру предопределённых макросов
# (syntax/parser.h), которые тянут генерируемый bison-заголовок parser.yy.h.
target_include_directories(lsp_lib PRIVATE ${CMAKE_BINARY_DIR}/syntax)

target_include_directories(lsp_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# #embed (встроенный trust/dsl.src) - расширение Clang в режиме C++23; выключаем
# предупреждение как в pipeline_lib (см. include/pipeline/CMakeLists.txt).
set_target_properties(lsp_lib PROPERTIES COMPILE_OPTIONS "-Wno-c23-extensions")

# -- trust-lsp: LSP server для trust-lang source mapping --
add_executable(trust-lsp
    ${PROJECT_SOURCE_DIR}/lsp/main.cpp
)

target_link_libraries(trust-lsp PRIVATE
    lsp_lib
    nlohmann_json::nlohmann_json
)

target_include_directories(trust-lsp PRIVATE
    ${PROJECT_INCLUDE_DIR}
)