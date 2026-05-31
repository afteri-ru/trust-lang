#pragma once

#include <optional>
#include <string>
#include <vector>

namespace trust::utils {

/// Читает содержимое ELF-секции по имени.
/// Возвращает std::nullopt если ELF невалиден, секция не найдена или ошибка ввода-вывода.
[[nodiscard]] std::optional<std::vector<unsigned char>> readElfSection(const std::string& elfPath, const std::string& sectionName);

} // namespace trust::utils