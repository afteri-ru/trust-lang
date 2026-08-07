#pragma once

#include <optional>
#include <string>
#include <vector>

namespace trust::utils {

/// Читает содержимое ELF-секции по имени.
/// Возвращает std::nullopt если ELF невалиден, секция не найдена или ошибка ввода-вывода.
[[nodiscard]] std::optional<std::vector<unsigned char>> readElfSection(const std::string& elfPath, const std::string& sectionName);

/// Читает содержимое секции из уже прочитанных в память байт ELF-файла.
/// Возвращает std::nullopt если данные не являются валидным ELF64 или секция не найдена.
[[nodiscard]] std::optional<std::vector<unsigned char>> readElfSectionFromBuffer(const std::vector<unsigned char>& elfData, const std::string& sectionName);

/// Читает содержимое секции из библиотеки, которой может быть как одиночный ELF-файл
/// (.so), так и статический ar-архив (.a). Для архива перебираются обычные ELF-члены
/// (trust_headers.o и т.п.), у каждого ищется запрошенная секция.
/// Возвращает std::nullopt если ни ELF, ни ar-архив, либо секция не найдена.
[[nodiscard]] std::optional<std::vector<unsigned char>> readSectionFromLibrary(const std::string& libraryPath, const std::string& sectionName);

} // namespace trust::utils