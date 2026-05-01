// include/stdlib/api_comparator.hpp
// Сравнение API между версиями стандартов C++
// Хранит данные по паттернам и версиям, проверяет совместимость

#ifndef STDLIB_API_COMPARATOR_HPP
#define STDLIB_API_COMPARATOR_HPP

#include "stdlib/analyzer.hpp"
#include "stdlib/name_utils.hpp"
#include "types/forward.hpp"

#include <map>
#include <string>
#include <vector>

namespace trust {

class ApiComparator {
  public:
    ApiComparator() = default;

    // Добавить записи для указанной версии.
    // Возвращает false, если обнаружена несовместимость с базовой версией.
    bool add_version(const std::string &pattern, LanguageVersion version, const std::vector<MethodInfo> &methods);

    // Проверить полную совместимость всех паттернов
    bool check_compatibility() const;

    // Получить данные для конкретного паттерна
    const std::map<uint8_t, std::vector<MethodInfo>> *get_versions(const std::string &pattern) const;

    // Получить базовую версию для паттерна
    uint8_t get_base_version(const std::string &pattern) const;

    // Сопоставить квалифицированное имя с паттерном поиска
    static std::string match_pattern(const std::string &qualified_name);

    // Преобразует glob-паттерн в безопасное имя файла
    static std::string pattern_to_filename(const std::string &pattern);

    // Получить имя версии для вывода (c++11, c++17, ...)
    static const char *version_name(LanguageVersion ver);

    // Получить суффикс версии для имён файлов
    static std::string version_suffix(LanguageVersion ver);

    // Получить все известные паттерны
    const std::map<std::string, std::string> &get_patterns() const;

  private:
    // pattern -> (version -> vector<MethodInfo>)
    std::map<std::string, std::map<uint8_t, std::vector<MethodInfo>>> data_;

    // pattern -> версия первой (базовой) записи
    std::map<std::string, uint8_t> base_version_;
};

} // namespace trust

#endif // STDLIB_API_COMPARATOR_HPP