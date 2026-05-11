#pragma once

#include "utils/error.hpp"

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

} // namespace trust::utils