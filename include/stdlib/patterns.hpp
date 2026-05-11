// include/stdlib/patterns.hpp
// Единый интерфейс для доступа к паттернам поиска и игнорирования
// Определено в trust_stdlib.cpp

#ifndef STDLIB_PATTERNS_HPP
#define STDLIB_PATTERNS_HPP

#include <map>
#include <set>
#include <string>
#include <vector>

namespace trust {

// Карта: шаблон поиска -> имя файла для результатов
const std::map<std::string, std::string>& get_search_patterns();

// Список игнорируемых паттернов (deprecated, removed, internal)
const std::set<std::string>& get_ignore_patterns();

// Информация об итераторах для конкретного паттерна контейнера
struct ContainerIteratorConfig {
    std::vector<std::string> iterator_names; // например: {"iterator", "const_iterator", "reverse_iterator", "const_reverse_iterator"}
};

// Получить конфигурацию итераторов для паттерна контейнера
// Возвращает nullptr, если паттерн не является контейнером
const ContainerIteratorConfig* get_container_iterator_config(const std::string& pattern);

} // namespace trust

#endif // STDLIB_PATTERNS_HPP
