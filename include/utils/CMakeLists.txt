# include/utils/CMakeLists.txt
# Utils - общие вспомогательные утилиты (единственный источник `FAULT`/`EXPECT`, пул строк,
# debug-трассировка; реализация сгруппирована по internal-подкаталогам io/proc/digest/strings/trace).

trust_component(utils
    INTERNAL_DIRS strings io proc trace digest
    PUBLIC_HEADERS
        utils.hpp error.hpp hash.hpp string_interner.hpp import_masks.hpp partial_switch.hpp
        string_view_hash.hpp strings.hpp
    SOURCES
        proc/cache.cpp proc/backtrace.cpp proc/elf.cpp proc/time.cpp
        io/io.cpp io/file_io.cpp io/zstd_compress.cpp
        digest/hash.cpp
    INCLUDE_PUBLIC ${PROJECT_INCLUDE_DIR}
    LINK_PUBLIC PkgConfig::ZSTD
    LINK_PRIVATE trust_llvm_dirs LLVMSupport
)
