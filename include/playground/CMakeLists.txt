# include/playground/CMakeLists.txt
# trust-playground: HTTP-сервер для playground (POST /run -> JSON).
# Единый бинарник с двумя режимами: --playground (балансировщик) и worker.

# Встроенные HTML-страницы (login / 403 / шаблон stats) встраиваются байтовыми массивами
# (стандартный C++, без расширений компилятора): .inc генерируются на configure-шаге
# (cmake/embed_assets.cmake), правка страницы перегенерирует их (CMAKE_CONFIGURE_DEPENDS).
set(_playground_generated "${CMAKE_BINARY_DIR}/generated/playground")
foreach(_page IN ITEMS login.html error403.html stats_page.html)
    string(REPLACE "." "_" _page_id "${_page}")
    trust_embed_asset("${_playground_generated}/${_page_id}.inc" FILE "${TRUST_SRC_DIR}/playground/server/${_page}")
endforeach()

trust_component(playground
    INTERNAL_DIRS app deploy http limits server
    SOURCES
        http/http.cpp http/config.cpp
        limits/rate_limiter.cpp limits/result_cache.cpp limits/stats_session_store.cpp
        limits/pow_guard.cpp limits/alert_notifier.cpp
        server/server.cpp server/server_endpoints.cpp server/server_stats.cpp
        server/server_http.cpp server/util.cpp server/worker.cpp
    LINK_PUBLIC lsp_lib nlohmann_json::nlohmann_json
    LINK_PRIVATE driver_lib
    # generated/ — байтовые .inc встроенных HTML-страниц.
    INCLUDE_PRIVATE ${CMAKE_BINARY_DIR}/generated
)

# ИНВАРИАНТ: встроенные страницы (login.html, error403.html, stats_page.html) подключены
# через сгенерированные .inc (trust_embed_asset выше); вручную зависимости не прописывать.

# -- trust-playground: HTTP server executable --
# nlohmann_json приходит PUBLIC-пропагацией от playground_lib.
trust_claim_sources(playground/app/main.cpp)
add_executable(trust-playground
    ${TRUST_SRC_DIR}/playground/app/main.cpp
)

target_link_libraries(trust-playground PRIVATE
    playground_lib
)

target_include_directories(trust-playground PRIVATE
    ${PROJECT_INCLUDE_DIR}
)
