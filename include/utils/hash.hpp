// include/utils/hash.hpp
// Хеширование содержимого (MD5 hex) — единая точка для пакетов (integrity), кеша --run и build.conf.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace trust::utils {

/// MD5 содержимого файла в hex (16 символов). nullopt при ошибке чтения.
std::optional<std::string> md5HexFile(const std::filesystem::path& path);

/// MD5 данных в hex (16 символов).
std::string md5Hex(std::string_view data);

} // namespace trust::utils
