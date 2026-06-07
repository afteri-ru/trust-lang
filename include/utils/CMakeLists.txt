# include/utils/CMakeLists.txt
# Utils — общие вспомогательные утилиты

# ── Utils library ──
add_library(utils_lib STATIC
    ${PROJECT_SOURCE_DIR}/utils/cache.cpp
    ${PROJECT_SOURCE_DIR}/utils/backtrace.cpp
    ${PROJECT_SOURCE_DIR}/utils/elf.cpp
    ${PROJECT_SOURCE_DIR}/utils/file_io.cpp
    ${PROJECT_SOURCE_DIR}/utils/io.cpp
    ${PROJECT_SOURCE_DIR}/utils/zstd_compress.cpp
)
target_include_directories(utils_lib PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(utils_lib PRIVATE trust_llvm_dirs LLVMSupport)
target_link_libraries(utils_lib PUBLIC PkgConfig::ZSTD)
