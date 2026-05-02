# include/diag/CMakeLists.txt
# Diag library — диагностика и опции

# ── Library ──
add_library(diag_lib STATIC
    ${PROJECT_SOURCE_DIR}/diag/context.cpp
    ${PROJECT_SOURCE_DIR}/diag/diag.cpp
    ${PROJECT_SOURCE_DIR}/diag/mapping.cpp
    ${PROJECT_SOURCE_DIR}/diag/options.cpp
)

target_link_libraries(diag_lib PUBLIC trust_include_dirs msgpack-c-static)

