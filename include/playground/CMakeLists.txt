# include/playground/CMakeLists.txt
# trust-playground: HTTP-сервер для playground (POST /run -> JSON).
# Единый бинарник с двумя режимами: --playground (балансировщик) и worker.

# -- Library (HTTP-ядро без сокетов; линкуется и в unit_tests) --
add_library(playground_lib STATIC
    ${PROJECT_SOURCE_DIR}/playground/http.cpp
    ${PROJECT_SOURCE_DIR}/playground/config.cpp
    ${PROJECT_SOURCE_DIR}/playground/util.cpp
    ${PROJECT_SOURCE_DIR}/playground/rate_limiter.cpp
    ${PROJECT_SOURCE_DIR}/playground/result_cache.cpp
    ${PROJECT_SOURCE_DIR}/playground/stats_session_store.cpp
    ${PROJECT_SOURCE_DIR}/playground/pow_guard.cpp
    ${PROJECT_SOURCE_DIR}/playground/alert_notifier.cpp
    ${PROJECT_SOURCE_DIR}/playground/server.cpp
    ${PROJECT_SOURCE_DIR}/playground/worker.cpp
)

target_link_libraries(playground_lib PUBLIC
    lsp_lib
    nlohmann_json::nlohmann_json
)

# #embed (встроенные HTML-страницы в server.cpp) - расширение Clang в режиме C++23; выключаем
# предупреждение как в lsp_lib/pipeline_lib (см. include/lsp/CMakeLists.txt).
set_target_properties(playground_lib PROPERTIES COMPILE_OPTIONS "-Wno-c23-extensions")

# ИНВАРИАНТ: зависимости от #embed-шаблонов (login.html, error403.html, stats_page.html)
# НЕ прописываются вручную (нет OBJECT_DEPENDS): clang >= 19 пишет #embed в depfile,
# CMake пересобирает server.cpp автоматически. НЕ добавлять OBJECT_DEPENDS.

target_include_directories(playground_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/include
)

# -- trust-playground: HTTP server executable --
add_executable(trust-playground
    ${PROJECT_SOURCE_DIR}/playground/main.cpp
)

target_link_libraries(trust-playground PRIVATE
    playground_lib
    nlohmann_json::nlohmann_json
)

target_include_directories(trust-playground PRIVATE
    ${PROJECT_INCLUDE_DIR}
)


