#pragma once

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace trust::utils {

// ══════════════════════════════════════════════════════════════
//  FileIO - утилитарный класс для работы с файлами
//
//  read<std::vector<char>>(path)    - читает файл целиком в std::vector<char>
//  read<std::string>(path)          - читает файл целиком в std::string
//  write(path, data)                - записывает данные в файл (создаёт или
//                                     перезаписывает). Поддерживает только
//                                     контейнеры с value_type = char.
// ══════════════════════════════════════════════════════════════

class FileIO {
  public:
    FileIO() = delete; // static only

    template <typename T>
    static std::optional<T> read(const std::string& path);

    template <typename Container>
    static bool write(const std::string& path, const Container& data);

  private:
    static std::optional<std::vector<char>> readImpl(const std::string& path);
};

// -- read --

template <typename T>
std::optional<T> FileIO::read(const std::string& path) {
    static_assert(std::is_same_v<T, std::vector<char>> || std::is_same_v<T, std::string>, "FileIO::read: only std::vector<char> or std::string are supported");
    auto data = readImpl(path);
    if (!data) {
        return std::nullopt;
    }
    if constexpr (std::is_same_v<T, std::string>) {
        return std::string(data->begin(), data->end());
    } else {
        return data;
    }
}

// -- write --

template <typename Container>
bool FileIO::write(const std::string& path, const Container& data) {
    using value_type = typename Container::value_type;
    static_assert(std::is_same_v<value_type, char>, "FileIO::write: only containers with value_type = char are supported");

    std::ofstream ofs(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!ofs) {
        return false;
    }

    ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

} // namespace trust::utils