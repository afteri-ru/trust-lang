// include/toolchain/hash.hpp
// Общая утилита конвейера: MD5-хэш файла для кеша --run / build.conf.
// Используется build.cpp, run.cpp и source_map.cpp — вынесена в отдельный модуль.
#pragma once

#include <filesystem>
#include <string>

namespace trust {
namespace toolchain {

/// MD5-хэш файла (hex, 16 символов; единый utils-хелпер). "error" при ошибке чтения.
std::string fileHash(const std::filesystem::path& path);

} // namespace toolchain
} // namespace trust
