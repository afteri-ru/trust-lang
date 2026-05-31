# include/diag/CMakeLists.txt
# Diag library — диагностика и опции

# ── Library ──
add_library(diag_lib STATIC
    ${PROJECT_SOURCE_DIR}/diag/context.cpp
    ${PROJECT_SOURCE_DIR}/diag/diag.cpp
    ${PROJECT_SOURCE_DIR}/diag/options.cpp
    ${PROJECT_SOURCE_DIR}/diag/reader.cpp
    ${PROJECT_SOURCE_DIR}/utils/cache.cpp
    ${PROJECT_SOURCE_DIR}/utils/backtrace.cpp
    ${PROJECT_SOURCE_DIR}/utils/elf.cpp
    ${PROJECT_SOURCE_DIR}/utils/file_io.cpp
)

target_include_directories(diag_lib SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(diag_lib PUBLIC trust_include_dirs ast_lib msgpack-c-static LLVMSupport ZLIB::ZLIB)

