// include/pipeline/io.hpp
// Общие мелкие утилиты конвейера: timestamp для шапки автогенерируемых файлов
// и MD5-хэш файла для кеша --run / build.conf. Используются build.cpp,
// run.cpp и source_map.cpp — вынесены в отдельный модуль, чтобы не дублировать.
#pragma once

#include <filesystem>
#include <string>

namespace trust {

/// Текущее время в ISO-стиле ("%Y-%m-%d %H:%M:%S %z") — для шапки генерируемых файлов.
std::string currentTimestamp();

/// MD5-хэш файла (llvm::MD5Hash, как в FileEntry::getHash). "error" при ошибке чтения.
std::string fileHash(const std::filesystem::path& path);

} // namespace trust
