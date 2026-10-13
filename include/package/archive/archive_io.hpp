#pragma once

// include/package/archive/archive_io.hpp
// Внутренний контракт tar-примитивов пакета: используется частями tar / resources / packing.
// Публичный API пакета - package/package.hpp.
//
// Контракт доставки: смена раскладки/сжатия ломает чтение уже опубликованных пакетов;
// версия формата хранится в манифесте (`formatVersion`, package/manifest.hpp).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trust::package::detail {

/// Записи архива (листинг `tar tzf`). false + out_error при ошибке tar.
bool listEntries(const std::filesystem::path& archive, std::vector<std::string>& out_entries, std::string& out_error);

/// Чтение одной записи архива в память (`tar xzf -O <entry>`).
bool readEntry(const std::filesystem::path& archive, std::string_view entry, std::string& out_content, std::string& out_error);

/// Распаковка архива в dest после проверки записей на path traversal.
bool extractArchive(const std::filesystem::path& archive, const std::filesystem::path& dest, std::string& out_error);

/// Безопасна ли запись tar: не абсолютный путь и без компонента '..'.
bool isSafeEntry(std::string_view entry);

/// Имя записи tar без ведущего "./".
std::string_view stripDotSlash(std::string_view entry);

} // namespace trust::package::detail
