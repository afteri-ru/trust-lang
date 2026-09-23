# include/sourcemap/CMakeLists.txt
# Source map: хранение исходников/вывода, маппинг вход↔выход, msgpack-сериализация.
# Отделено от diag: чистые диагностики/опции не тянут msgpack/zstd/elf.

add_library(sourcemap_lib STATIC
    ${PROJECT_SOURCE_DIR}/sourcemap/mapper.cpp
    ${PROJECT_SOURCE_DIR}/sourcemap/reader.cpp
)

target_link_libraries(sourcemap_lib PUBLIC
    trust_include_dirs
    diag_lib
    location_lib
    utils_lib
    msgpack-c-static
)
# reader.cpp читает LLVM-типы (ArrayRef) для распаковки source map.
target_link_libraries(sourcemap_lib PRIVATE trust_llvm_dirs LLVMSupport)

# Shared precompiled header (opt-in: TRUST_USE_PCH; no-op when OFF).
target_link_libraries(sourcemap_lib PRIVATE trust_pch)
