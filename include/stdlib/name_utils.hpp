// include/stdlib/name_utils.hpp
// Утилиты для работы с квалифицированными именами (std::vector::push_back)
// Консолидирует дублирующуюся логику из разных модулей

#ifndef STDLIB_NAME_UTILS_HPP
#define STDLIB_NAME_UTILS_HPP

#include <set>
#include <string>

namespace trust {

// Извлечь короткое имя: push_back из std::vector::push_back
std::string short_name(const std::string &qualified);

// Извлечь родительский класс/namespace: std::vector из std::vector::push_back
std::string class_name(const std::string &qualified);

// Убрать шаблонные аргументы: std::vector<_Tp, _Alloc> -> std::vector
std::string remove_template_args(const std::string &name);

// Проверить, является ли имя внутренним (любой компонент начинается с '_')
bool is_internal_name(const std::string &qualified);

// Проверить, попадает ли имя под какой-либо ignore pattern
bool matches_ignore_pattern(const std::string &name);

// Посчитать вхождения подстроки (вспомогательная функция)
size_t count_occurrences(const std::string &str, const std::string &sub);

} // namespace trust

#endif // STDLIB_NAME_UTILS_HPP