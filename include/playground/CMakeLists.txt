# include/playground/CMakeLists.txt
# trust-playground: HTTP-сервер для playground (POST /run -> JSON).
# Единый бинарник с двумя режимами: --playground (балансировщик) и worker.

trust_component(playground
    INTERNAL_DIRS server http limits deploy
    SOURCES
        http.cpp config.cpp util.cpp rate_limiter.cpp result_cache.cpp stats_session_store.cpp
        pow_guard.cpp alert_notifier.cpp server.cpp server_endpoints.cpp server_stats.cpp
        server_http.cpp worker.cpp
    LINK_PUBLIC lsp_lib nlohmann_json::nlohmann_json
    LINK_PRIVATE driver_lib
    # #embed (встроенные HTML-страницы в server.cpp) - расширение Clang в режиме C++23.
    COMPILE_OPTIONS -Wno-c23-extensions
)

# ИНВАРИАНТ: зависимости от #embed-шаблонов (login.html, error403.html, stats_page.html)
# НЕ прописываются вручную (нет OBJECT_DEPENDS): clang >= 19 пишет #embed в depfile.

# -- trust-playground: HTTP server executable --
# nlohmann_json приходит PUBLIC-пропагацией от playground_lib.
trust_claim_sources(playground/main.cpp)
add_executable(trust-playground
    ${TRUST_SRC_DIR}/playground/main.cpp
)

target_link_libraries(trust-playground PRIVATE
    playground_lib
)

target_include_directories(trust-playground PRIVATE
    ${PROJECT_INCLUDE_DIR}
)
