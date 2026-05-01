// src/stdlib/api_comparator.cpp
// Реализация сравнения API между версиями стандартов C++

#include "stdlib/api_comparator.hpp"
#include "stdlib/patterns.hpp"
#include "stdlib/matcher.hpp"

#include <iostream>

namespace trust {

// ─────────────────────────────────────────────────────────────
// Получить все известные паттерны
// ─────────────────────────────────────────────────────────────
const std::map<std::string, std::string> &ApiComparator::get_patterns() const {
    return get_search_patterns();
}

// ─────────────────────────────────────────────────────────────
// Лениво инициализируемый кэш StringMatcher для каждого составного ключа
// ─────────────────────────────────────────────────────────────
static const std::map<std::string, StringMatcher> &get_pattern_matchers() {
    static std::map<std::string, StringMatcher> matchers;
    static bool initialized = false;
    if (!initialized) {
        for (const auto &[key, _] : get_search_patterns()) {
            matchers.emplace(key, StringMatcher(key, ','));
        }
        initialized = true;
    }
    return matchers;
}

std::string ApiComparator::match_pattern(const std::string &qualified_name) {
    // Нормализованное имя без template args
    std::string normalized = trust::remove_template_args(qualified_name);

    // 1. Проверка через StringMatcher (glob + exact) для составных ключей
    for (const auto &[key, matcher] : get_pattern_matchers()) {
        if (matcher.MatchesName(normalized)) {
            return key;
        }
    }

    // 2. Prefix matching для методов классов (разбиваем ключ на под-паттерны)
    std::vector<std::string> parts;
    for (const auto &[key, _] : get_search_patterns()) {
        parts.clear();
        trust::SplitString(key, ',', &parts);
        for (const auto &part : parts) {
            std::string prefix = part + "::";
            if (!prefix.empty() && normalized.rfind(prefix, 0) == 0) {
                return key;
            }
        }
    }

    // 3. Паттерн "::*" — свободные функции в пространствах имён
    // Например: std::swap, std::make_unique (ровно один :: в имени)
    // Также глобальные C-функции: printf, fileno (0 :: в имени)
    for (const auto &[key, _] : get_search_patterns()) {
        if (key == "::*") {
            size_t count = trust::count_occurrences(qualified_name, "::");
            if (count == 1) {
                // Исключаем конструкторы/деструкторы (type::type или type::~type)
                auto pos = qualified_name.find("::");
                if (pos != std::string::npos) {
                    std::string first = qualified_name.substr(0, pos);
                    std::string second = qualified_name.substr(pos + 2);
                    // Убираем ~ для деструкторов
                    if (!second.empty() && second[0] == '~')
                        second = second.substr(1);
                    if (first == second)
                        continue; // это конструктор/деструктор, пропускаем
                }
                return key;
            }
            // Глобальные функции без пространства имён (0 ::)
            // printf, fileno, atoi, и т.д.
            if (count == 0) {
                return key;
            }
        }
    }

    return {};
}

// ─────────────────────────────────────────────────────────────
// Pattern to filename
// ─────────────────────────────────────────────────────────────
std::string ApiComparator::pattern_to_filename(const std::string &pattern) {
    const auto &patterns = get_search_patterns();
    auto it = patterns.find(pattern);
    if (it != patterns.end()) {
        return it->second;
    }
    return pattern;
}

// ─────────────────────────────────────────────────────────────
// Имя версии для вывода
// ─────────────────────────────────────────────────────────────
const char *ApiComparator::version_name(LanguageVersion ver) {
    return language_version_string(ver);
}

// ─────────────────────────────────────────────────────────────
// Суффикс версии для имён файлов
// ─────────────────────────────────────────────────────────────
std::string ApiComparator::version_suffix(LanguageVersion ver) {
    return language_version_string(ver);
}

// ─────────────────────────────────────────────────────────────
// Получить данные для паттерна
// ─────────────────────────────────────────────────────────────
const std::map<uint8_t, std::vector<MethodInfo>> *ApiComparator::get_versions(const std::string &pattern) const {
    auto it = data_.find(pattern);
    if (it != data_.end() && !it->second.empty()) {
        return &it->second;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────
// Получить базовую версию
// ─────────────────────────────────────────────────────────────
uint8_t ApiComparator::get_base_version(const std::string &pattern) const {
    auto it = base_version_.find(pattern);
    if (it != base_version_.end()) {
        return it->second;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────
// Добавить записи для версии с проверкой совместимости
// ─────────────────────────────────────────────────────────────
bool ApiComparator::add_version(const std::string &pattern, LanguageVersion version, const std::vector<MethodInfo> &methods) {
    uint8_t ver_num = static_cast<uint8_t>(version);
    const char *ver_name = version_name(version);

    // Проверка: версия должна быть больше всех уже добавленных
    auto &ver_map = data_[pattern];
    if (!ver_map.empty()) {
        uint8_t max_ver = ver_map.rbegin()->first;
        if (ver_num < max_ver) {
            std::cerr << "Error: versions must be in ascending order for pattern '" << pattern << "'\n";
            std::cerr << "  Attempted to add " << ver_name << " after " << version_name(static_cast<LanguageVersion>(max_ver)) << "\n";
            return false;
        }
    }

    // Если это первая версия — запоминаем как базовую
    if (base_version_.find(pattern) == base_version_.end()) {
        base_version_[pattern] = ver_num;
    }

    // Сравниваем с базовой версией
    uint8_t base_ver = base_version_[pattern];
    const auto &base_methods = data_[pattern][base_ver];

    // Создаём карту базовых методов: qualified_name -> normalized_signature
    std::map<std::string, std::string> base_map;
    for (const auto &info : base_methods) {
        base_map[info.qualified_name] = info.normalized_signature;
    }

    // Создаём карту текущей версии
    std::map<std::string, std::string> current_map;
    for (const auto &info : methods) {
        current_map[info.qualified_name] = info.normalized_signature;
    }

    // Проверяем: все методы из базы должны присутствовать
    bool has_error = false;
    for (const auto &[name, base_sig] : base_map) {
        // Проверяем, попадает ли имя под какой-либо ignore pattern
        if (trust::matches_ignore_pattern(name))
            continue;

        auto it = current_map.find(name);
        if (it == current_map.end()) {
            std::cerr << "Error: API incompatibility in " << pattern << "\n";
            std::cerr << "  method '" << name << "' missing in " << ver_name << "\n";
            has_error = true;
        }
    }

    if (has_error) {
        std::cerr << "Analysis aborted. Output file not created.\n";
        return false;
    }

    // Сохраняем записи
    data_[pattern][ver_num] = methods;
    return true;
}

// ─────────────────────────────────────────────────────────────
// Проверить полную совместимость
// ─────────────────────────────────────────────────────────────
bool ApiComparator::check_compatibility() const {
    for (const auto &[pattern, ver_map] : data_) {
        if (ver_map.empty())
            continue;

        const auto &base_methods = ver_map.begin()->second;

        std::map<std::string, std::string> base_map;
        for (const auto &info : base_methods) {
            base_map[info.qualified_name] = info.normalized_signature;
        }

        for (auto vit = std::next(ver_map.begin()); vit != ver_map.end(); ++vit) {
            for (const auto &info : vit->second) {
                auto it = base_map.find(info.qualified_name);
                if (it == base_map.end()) {
                    // Новый метод — это нормально
                    continue;
                }
            }
        }
    }
    return true;
}

} // namespace trust