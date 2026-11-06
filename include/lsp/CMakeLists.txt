# include/lsp/CMakeLists.txt
# LSP: Trust-lang Language Server Protocol (source mapping) + executable `trust-lsp`.

# Ассеты playground встраиваются байтовыми массивами (стандартный C++, без расширений
# компилятора): .inc генерируются на configure-шаге (cmake/embed_assets.cmake), правка ассета
# перегенерирует их через CMAKE_CONFIGURE_DEPENDS и пересоберёт TU.
set(_lsp_generated "${CMAKE_BINARY_DIR}/generated/lsp")
foreach(_lsp_asset IN ITEMS trust.monarch.js glue.js playground.css playground.html playground_page.html)
    string(REPLACE "." "_" _lsp_asset_id "${_lsp_asset}")
    trust_embed_asset("${_lsp_generated}/${_lsp_asset_id}.inc" FILE "${TRUST_SRC_DIR}/lsp/emit/${_lsp_asset}")
endforeach()

trust_component(lsp
    INTERNAL_DIRS app catalog emit protocol services
    PUBLIC_HEADERS html_emit.h lsp_protocol.h
    SOURCES
        protocol/lsp_protocol.cpp emit/html_emit.cpp
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
    # generated/ — байтовые .inc встроенных ассетов (cmake/embed_assets.cmake).
    INCLUDE_PRIVATE ${CMAKE_BINARY_DIR}/syntax ${CMAKE_BINARY_DIR}/generated
)

# ИНВАРИАНТ: встроенные ассеты (glue.js, trust.monarch.js, playground.css/html,
# playground_page.html) подключены через сгенерированные .inc (trust_embed_asset выше);
# вручную зависимости не прописывать.

# -- trust-lsp: LSP server для trust-lang source mapping --
trust_claim_sources(lsp/app/main.cpp)
add_executable(trust-lsp
    ${TRUST_SRC_DIR}/lsp/app/main.cpp
)

target_link_libraries(trust-lsp PRIVATE
    lsp_lib
    builddriver_lib
    nlohmann_json::nlohmann_json
)

target_include_directories(trust-lsp PRIVATE
    ${PROJECT_INCLUDE_DIR}
)
