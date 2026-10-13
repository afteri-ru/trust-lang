# include/sourcemap/CMakeLists.txt
# Source map: хранение исходников/вывода, маппинг вход↔выход, msgpack-сериализация.
# Отделено от diag: чистые диагностики/опции не тянут msgpack/zstd/elf.

trust_component(sourcemap
    INTERNAL_DIRS serialize store
    PUBLIC_HEADERS mapper.hpp protocol.hpp writer.hpp
    SOURCES
        store/output_buffer.cpp store/writer_files.cpp store/writer_mapping.cpp store/writer_to_reader.cpp
        serialize/reader.cpp serialize/reader_codec.cpp serialize/reader_lookup.cpp
    LINK_PUBLIC trust_include_dirs diag_lib location_lib utils_lib msgpack-c-static
    # reader.cpp читает LLVM-типы (ArrayRef) для распаковки source map.
    LINK_PRIVATE trust_llvm_dirs LLVMSupport
)
