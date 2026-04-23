# include/diag/CMakeLists.txt
# Diag library — диагностика и опции

# Report inherited configuration from parent project
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "[diag] Inheriting from parent: compiler=${CMAKE_CXX_COMPILER}, standard=${CMAKE_CXX_STANDARD}")
endif()

# ── Библиотека diag_lib ─────────────────────────────────────────────────
add_library(diag_lib STATIC
    ${CMAKE_SOURCE_DIR}/src/diag/context.cpp
    ${CMAKE_SOURCE_DIR}/src/diag/diag.cpp
    ${CMAKE_SOURCE_DIR}/src/diag/options.cpp
)

target_include_directories(diag_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

