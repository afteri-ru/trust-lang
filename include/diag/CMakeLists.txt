# include/diag/CMakeLists.txt
# Diag library — диагностика и опции

# ── Library ──
add_library(diag_lib STATIC
    ${PROJECT_SOURCE_DIR}/src/diag/context.cpp
    ${PROJECT_SOURCE_DIR}/src/diag/diag.cpp
    ${PROJECT_SOURCE_DIR}/src/diag/options.cpp
)

target_link_libraries(diag_lib PUBLIC trust_include_dirs)

