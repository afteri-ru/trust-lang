# include/lsp/CMakeLists.txt
# LSP: Trust-lang Language Server Protocol (source mapping) + executable `trust-lsp`.

trust_component(lsp
    INTERNAL_DIRS catalog services
    PUBLIC_HEADERS html_emit.h lsp_protocol.h
    SOURCES
        lsp_protocol.cpp html_emit.cpp
        catalog/trust_lsp.cpp catalog/builtin_catalog.cpp
        services/document_manager.cpp services/hover_service.cpp services/navigation_service.cpp
        services/analysis_service.cpp services/code_action.cpp services/formatting_service.cpp
        services/completion.cpp
    LINK_PUBLIC
        trust_include_dirs driver_lib symbols_lib session_lib sourcemap_lib semantic_lib
    LINK_PRIVATE
        transpiler_lib pipeline_lib formatter_lib nlohmann_json::nlohmann_json
    # Для доступа к макропроцессору (syntax/macro.h) и реестру предопределённых макросов
    # (syntax/parser.h), которые тянут генерируемый bison-заголовок parser.yy.h.
    INCLUDE_PRIVATE ${CMAKE_BINARY_DIR}/syntax
    # #embed (ресурсы playground: CSS/HTML/JS) - расширение Clang в режиме C++23.
    COMPILE_OPTIONS -Wno-c23-extensions
)

# ИНВАРИАНТ: зависимости от #embed-файлов (glue.js, trust.monarch.js, playground.css/html,
# playground_page.html) НЕ прописываются вручную (нет OBJECT_DEPENDS):
# clang >= 19 пишет #embed в depfile, и CMake пересобирает .cpp автоматически. НЕ добавлять OBJECT_DEPENDS.

# -- trust-lsp: LSP server для trust-lang source mapping --
trust_claim_sources(lsp/main.cpp)
add_executable(trust-lsp
    ${TRUST_SRC_DIR}/lsp/main.cpp
)

target_link_libraries(trust-lsp PRIVATE
    lsp_lib
    nlohmann_json::nlohmann_json
)

target_include_directories(trust-lsp PRIVATE
    ${PROJECT_INCLUDE_DIR}
)
