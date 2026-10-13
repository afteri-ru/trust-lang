#pragma once

#include "utils/error.hpp"
// Umbrella: детали сгруппированы в internal-подкаталогах (io/ — ввод-вывод/транспорт/uri/zstd,
// proc/ — ELF/backtrace/кеш/время, trace/ — отладочная трассировка, strings/ — строковые утилиты).
// Другие компоненты включают ТОЛЬКО этот заголовок (и, при необходимости, отдельные публичные
// заголовки корня: error/hash/string_interner/import_masks/partial_switch/string_view_hash/strings).
#include "utils/io/file_io.hpp"
#include "utils/io/io.hpp"
#include "utils/io/socket_io.hpp"
#include "utils/io/transport.hpp"
#include "utils/io/uri.hpp"
#include "utils/io/zstd_compress.hpp"
#include "utils/proc/backtrace.hpp"
#include "utils/proc/cache.hpp"
#include "utils/proc/elf.hpp"
#include "utils/proc/time.hpp"
#include "utils/trace/trace.hpp"
#include "utils/trace/trace_options.hpp"

#include <filesystem>
#include <string_view>
#include <system_error>

namespace trust::utils {

// -----------------------------------------------------------------------
// resolveTempDir -- временный каталог с подкаталогом .trust
// -----------------------------------------------------------------------
//
// Алгоритм:
//   baseDir пуст          → temp_directory_path() / ".trust"
//   baseDir абсолютный    → fs::absolute(baseDir).lexically_normal() / ".trust"
//   baseDir относительный → то же самое (absolute раскрывает в абсолютный)
//   create == true        → создаёт каталог (create_directories) + проверка
//
// Возвращает: нормализованный путь к каталогу ".trust"
// Бросает:   std::filesystem::filesystem_error если create == true и создать не удалось

inline std::filesystem::path resolveTempDir(std::string_view baseDir = {}, bool create = false) {
    namespace fs = std::filesystem;

    fs::path result;
    if (baseDir.empty()) {
        result = fs::temp_directory_path() / ".trust";
    } else {
        result = fs::absolute(fs::path(baseDir)).lexically_normal() / ".trust";
    }

    if (create) {
        std::error_code ec;
        fs::create_directories(result, ec);
        if (ec || !fs::is_directory(result)) {
            throw fs::filesystem_error("failed to create temp directory", result, ec ? ec : std::make_error_code(std::errc::io_error));
        }
    }

    return result;
}

// -----------------------------------------------------------------------
// executablePath -- путь к реальному исполняемому файлу
// -----------------------------------------------------------------------
//
// Симлинки раскрываются через /proc/self/exe. Пустой путь - определить не удалось
// (вызывающий решает, что делать; тихого fallback нет).

inline std::filesystem::path executablePath() {
    std::error_code ec;
    std::filesystem::path exe = std::filesystem::canonical("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    return exe;
}

} // namespace trust::utils