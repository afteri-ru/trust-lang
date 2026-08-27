# include/diag/CMakeLists.txt
# Diag library - диагностика и опции

# -- Library --
add_library(diag_lib STATIC
    ${PROJECT_SOURCE_DIR}/diag/context.cpp
    ${PROJECT_SOURCE_DIR}/diag/diag.cpp
    ${PROJECT_SOURCE_DIR}/diag/options.cpp
    ${PROJECT_SOURCE_DIR}/diag/registry.cpp
    ${PROJECT_SOURCE_DIR}/diag/reader.cpp
    ${PROJECT_SOURCE_DIR}/diag/mapper.cpp
)

target_link_libraries(diag_lib PRIVATE trust_llvm_dirs)
target_link_libraries(diag_lib PUBLIC trust_include_dirs location_lib utils_lib msgpack-c-static)
target_link_libraries(diag_lib PRIVATE LLVMSupport)

