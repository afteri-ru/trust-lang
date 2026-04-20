# include/diag/CMakeLists.txt
# Diag library — диагностика и опции
# Собирается только как subdirectory (тесты управляются из корневого CMakeLists.txt)

# Report inherited configuration from parent project
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "[diag] Inheriting from parent: compiler=${CMAKE_CXX_COMPILER}, standard=${CMAKE_CXX_STANDARD}")
endif()

# ── Библиотека diag_lib ─────────────────────────────────────────────────
add_library(diag_lib STATIC
    context.cpp
    diag.cpp
    options.cpp
)

target_include_directories(diag_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)