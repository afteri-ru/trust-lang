// src/stdlib/formatter.cpp
// Реализация форматирования результатов анализа API

#include "stdlib/formatter.hpp"
#include "stdlib/analyzer.hpp"
#include "stdlib/name_utils.hpp"
#include "stdlib/patterns.hpp"
#include "stdlib/matcher.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

namespace trust {

OutputFormatter::OutputFormatter(const ApiComparator& comparator)
: comparator_(comparator) {
}

// ─────────────────────────────────────────────────────────────
// Helper: извлечь родительский класс из qualified_name метода
// std::map<int, std::string, ...>::begin -> std::map<int, std::string, ...>
// ─────────────────────────────────────────────────────────────
static std::string get_iterator_parent(const std::string& qualified_name) {
    // Для методов вроде begin/end: ищем имя метода и извлекаем всё до него
    // std::map<int, std::less<int>>::begin -> parent = std::map<int, std::less<int>>
    auto pos = qualified_name.rfind("::");
    if (pos == std::string::npos)
        return {};

    std::string method_name = qualified_name.substr(pos + 2);
    // Только для методов-итераторов
    if (method_name != "begin" && method_name != "end" && method_name != "cbegin" && method_name != "cend" && method_name != "rbegin" &&
        method_name != "rend" && method_name != "crbegin" && method_name != "crend") {
        return {};
    }

    return qualified_name.substr(0, pos);
}

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────
// short_name — делегируем в name_utils, но оставляем static для совместимости интерфейса
// (OutputFormatter::short_name нужен для API)
std::string OutputFormatter::short_name(const std::string& full) {
    auto pos = full.rfind("::");
    return (pos != std::string::npos) ? full.substr(pos + 2) : full;
}

// class_name — делегируем в name_utils
std::string OutputFormatter::class_name(const std::string& full) {
    auto pos = full.rfind("::");
    if (pos == std::string::npos)
        return full;
    auto prev_pos = full.rfind("::", pos - 1);
    if (prev_pos == std::string::npos)
        return full.substr(0, pos);
    return full.substr(0, pos);
}

// ─────────────────────────────────────────────────────────────
// Записать один файл для паттерна
// ─────────────────────────────────────────────────────────────
void OutputFormatter::write_one(std::ofstream& out, const std::string& pattern) const {
    const auto* ver_map_ptr = comparator_.get_versions(pattern);
    if (!ver_map_ptr || ver_map_ptr->empty())
        return;

    const auto& ver_map = *ver_map_ptr;
    const auto& base_methods = ver_map.begin()->second;

    // out << "# Base: " << ApiComparator::version_name(static_cast<LanguageVersion>(base_ver)) << "\n";
    // out << "\n";

    // Группируем методы по классам
    struct MethodGroup {
        std::string full_name;
        std::string signature;
        std::vector<std::uint8_t> versions;
    };

    // class_name -> short_name -> MethodGroup
    std::map<std::string, std::map<std::string, MethodGroup>> all_by_class;

    // Отдельно собираем TypeAlias
    struct AliasGroup {
        std::string full_name;
        std::string underlying_type;
        std::vector<std::uint8_t> versions;
    };
    std::map<std::string, AliasGroup> aliases_by_short;

    for (const auto& [ver, methods] : ver_map) {
        for (const auto& info : methods) {
            // Исключаем функции, попадающие под ignore patterns
            if (matches_ignore_pattern(info.qualified_name)) {
                continue;
            }

            // TypeAlias обрабатываем отдельно
            if (info.category == DeclCategory::TypeAlias) {
                std::string sn = short_name(info.qualified_name);
                auto& alias_group = aliases_by_short[sn];
                if (alias_group.full_name.empty()) {
                    alias_group.full_name = info.qualified_name;
                    // For TypeAlias, return_type содержит подставленный тип
                    alias_group.underlying_type = info.return_type;
                }
                alias_group.versions.push_back(ver);
                continue;
            }

            std::string cls = class_name(info.qualified_name);
            std::string sn = short_name(info.qualified_name);

            // Для глобальных функций (без ::) используем "global" как класс
            bool is_global = (cls == sn);
            if (is_global) {
                cls = "global";
            }

            auto& group = all_by_class[cls][sn];
            if (group.full_name.empty()) {
                group.full_name = info.qualified_name;
                group.signature = info.normalized_signature;
            }
            group.versions.push_back(ver);
        }
    }

    // Выводим методы
    for (const auto& [cls, method_map] : all_by_class) {
        out << "class " << cls << "\n";
        for (const auto& [sn, group] : method_map) {
            if (group.versions.empty())
                continue;

            // Собираем уникальные сигнатуры по версиям
            std::map<std::string, uint8_t> sig_versions; // normalized_sig -> earliest_version
            for (const auto& [ver, methods] : ver_map) {
                for (const auto& info : methods) {
                    if (matches_ignore_pattern(info.qualified_name))
                        continue;
                    std::string item_cls = class_name(info.qualified_name);
                    std::string item_sn = short_name(info.qualified_name);
                    // Для глобальных функций cls == "global", item_cls == item_sn
                    bool item_is_global = (item_cls == item_sn);
                    if (item_is_global)
                        item_cls = "global";
                    if (item_sn == sn && item_cls == cls) {
                        std::string norm = info.normalized_signature;
                        if (sig_versions.find(norm) == sig_versions.end()) {
                            sig_versions[norm] = ver;
                        }
                    }
                }
            }

            if (sig_versions.size() == 1) {
                out << "  " << group.full_name << ": " << group.signature << "\n";
            } else {
                for (const auto& [sig, ver] : sig_versions) {
                    out << "  " << group.full_name << ": " << sig << "  # " << ApiComparator::version_name(static_cast<LanguageVersion>(ver)) << "\n";
                }
            }
        }
        out << "\n";
    }

    // Выводим TypeAlias в отдельной секции
    if (!aliases_by_short.empty()) {
        out << "--- Type Aliases ---\n";
        for (const auto& [sn, alias_group] : aliases_by_short) {
            if (alias_group.versions.empty())
                continue;
            if (!alias_group.underlying_type.empty()) {
                out << "  " << alias_group.full_name << " = " << alias_group.underlying_type << "\n";
            } else {
                out << "  " << alias_group.full_name << "\n";
            }
        }
        out << "\n";
    }

    // Новые методы по версиям
    for (auto vit = std::next(ver_map.begin()); vit != ver_map.end(); ++vit) {
        const auto& methods = vit->second;

        std::set<std::string> base_keys;
        for (const auto& info : base_methods) {
            base_keys.insert(class_name(info.qualified_name) + "::" + short_name(info.qualified_name));
        }

        std::map<std::string, std::vector<const MethodInfo*>> new_by_class;
        std::set<std::string> added_keys;

        for (const auto& info : methods) {
            // Исключаем функции, попадающие под ignore patterns
            if (matches_ignore_pattern(info.qualified_name)) {
                continue;
            }

            std::string cls = class_name(info.qualified_name);
            std::string sn = short_name(info.qualified_name);
            std::string key = cls + "::" + sn;

            if (base_keys.find(key) == base_keys.end() && added_keys.find(key) == added_keys.end()) {
                new_by_class[cls].push_back(&info);
                added_keys.insert(key);
            }
        }

        if (!new_by_class.empty()) {
            out << "--- " << ApiComparator::version_name(static_cast<LanguageVersion>(vit->first)) << " new ---\n";
            for (const auto& [cls, method_ptrs] : new_by_class) {
                for (const auto* info : method_ptrs) {
                    out << "+ " << info->qualified_name << ": " << info->normalized_signature << "\n";
                }
            }
            out << "\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Записать все файлы
// ─────────────────────────────────────────────────────────────
void OutputFormatter::write_all(const std::string& output_dir) const {
    for (const auto& [pattern, _] : comparator_.get_patterns()) {
        const auto* ver_map_ptr = comparator_.get_versions(pattern);
        if (!ver_map_ptr || ver_map_ptr->empty())
            continue;

        std::string path = output_dir + "/" + ApiComparator::pattern_to_filename(pattern) + ".matches.txt";

        std::ofstream ofs(path);
        if (!ofs) {
            std::cerr << "Error: cannot open output file: " << path << "\n";
            continue;
        }

        write_one(ofs, pattern);
    }
}

// ─────────────────────────────────────────────────────────────
// Записать единый файл iterators.txt
// Генерирует итераторы на основе конфигурации паттернов контейнеров
// ─────────────────────────────────────────────────────────────
void OutputFormatter::write_iterators_file(const std::string& output_dir) const {
    std::string path = output_dir + "/iterators.txt";
    std::ofstream out(path);
    if (!out) {
        std::cerr << "Error: cannot open output file: " << path << "\n";
        return;
    }

    out << "# ============================================\n";
    out << "# All Iterators by Pattern\n";
    out << "# ============================================\n\n";

    for (const auto& [pattern, _] : comparator_.get_patterns()) {
        const auto* iter_config = get_container_iterator_config(pattern);
        if (!iter_config)
            continue;

        const auto* ver_map_ptr = comparator_.get_versions(pattern);
        if (!ver_map_ptr)
            continue;

        // Проверяем, есть ли у контейнера хотя бы один метод — тогда он существует
        bool has_any_method = false;
        for (const auto& [ver, methods] : *ver_map_ptr) {
            if (!methods.empty()) {
                has_any_method = true;
                break;
            }
        }
        if (!has_any_method)
            continue;

        out << "# --- " << pattern << " ---\n";

        // Для каждого итератора в конфигурации генерируем конкретные типы
        for (const auto& iter_name : iter_config->iterator_names) {
            // Для wildcard паттернов: std::*map -> std::map, std::unordered_map и т.д.
            if (pattern.find('*') != std::string::npos) {
                // Раскрываем wildcard через реальные найденные классы
                std::set<std::string> actual_classes;
                for (const auto& [ver, methods] : *ver_map_ptr) {
                    for (const auto& info : methods) {
                        std::string parent = get_iterator_parent(info.qualified_name);
                        if (!parent.empty() && matches_ignore_pattern(parent) == false) {
                            actual_classes.insert(parent);
                        }
                    }
                }

                // Заменяем * в паттерне на реальные части
                for (const auto& cls : actual_classes) {
                    // Извлекаем часть между std:: и последним компонентом
                    // std::*map -> ищем "map", "unordered_map" и т.д.
                    // pattern: std::*map, cls: std::map
                    if (trust::PatternMatchesString(cls, pattern.data(), pattern.data() + pattern.size())) {
                        out << cls << "::" << iter_name << "\n";
                    }
                }
            } else {
                // Точный паттерн: std::vector
                out << pattern << "::" << iter_name << "\n";
            }
        }
        out << "\n";
    }
}

} // namespace trust
