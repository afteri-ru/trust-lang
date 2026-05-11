// src/stdlib/trust_stdlib.cpp
// Входной файл для AST-анализа стандартной библиотеки
// При изменении этого файла автоматически пересоберётся и запустится stdlib
// Содержит #include заголовков, которые анализирует Clang
//
// Заголовки подключаются в зависимости от версии C++

#include <cstdio>
#include <set>
#include <string>
#include <map>
#include <vector>
#include <iterator>

// ── Условное подключение заголовков в зависимости от версии C++ ──
#if __cplusplus >= 201103L
// C++11 — базовые заголовки (unordered_map доступен, но может отличаться в некоторых реализациях)
#include <unordered_map>
#endif

#include "stdlib/patterns.hpp"

namespace trust {

// ─────────────────────────────────────────────────────────────
// Паттерны поиска
// ─────────────────────────────────────────────────────────────
static const std::map<std::string, std::string>& all_search_patterns() {
    static const std::map<std::string, std::string> patterns{
        //{"::*", "functions"},
        {"::printf,abs,ato*,rand*,f*", "functions"},
        // jhgjhg
        {"*initializer_list*", "extra"},

        // {"std::*map", "std_map"},
        // {"std::*set", "std_set"},
        {"std::vector", "std_vector"},
        // {"std::list", "std_list"},
        // {"std::deque", "std_deque"},
    };
    return patterns;
}

const std::map<std::string, std::string>& get_search_patterns() {
    return all_search_patterns();
}

// ─────────────────────────────────────────────────────────────
// Игнорируемые паттерны
// ─────────────────────────────────────────────────────────────
const std::set<std::string>& get_ignore_patterns() {
    static const std::set<std::string> removed = {
        // удалено в C++20
        "std::unexpected",
        "std::get_unexpected",
        "std::set_unexpected",
        "std::not1",
        "std::not2",

        // удалён в C++17
        "std::auto_ptr",
        "std::random_shuffle",
        "std::bind1st",
        "std::bind2nd",
        "std::ptr_fun",
        "std::mem_fun",
        "std::mem_fun_ref",
        "gets",

        // Type Traits (C++ type predicates)
        "std::is_*",             // is_abstract, is_trivial, is_integral, is_class, ...
        "std::has_*",            // has_virtual_destructor, has_unique_object_representations, ...
        "std::enable_if*",       // enable_if, enable_if_t
        "std::conditional*",     // conditional, conditional_t
        "std::underlying_type*", // underlying_type, underlying_type_t
        "std::common_type*",     // common_type, common_type_t
        "std::__is_*",           // внутренние реализации libstdc++
        "std::__has_*",          // внутренние реализации libstdc++

        "std::uninitialized*",
        "std::unwrap*",
        "std::common*",
        "std::pointer*",
        "std::mem*",
        "*decltype*",

        // Не требуются в выводе
        "std::addressof",
        "std::basic_*",
        "pthread_*",
        "std::decay",
        "std::declval",
        "std::destroy*",
        "std::forward*",
        "std::make_*",
        "std::move*",
        "std::ref*",
        "std::remove*",
        "std::swap*",

    };
    return removed;
}

// ─────────────────────────────────────────────────────────────
// Конфигурация итераторов для контейнеров
// ─────────────────────────────────────────────────────────────

// Все 4 итератора (ordered контейнеры: vector, list, deque, array, map, multimap)
const std::vector<std::string> full_iterators = {"iterator", "const_iterator", "reverse_iterator", "const_reverse_iterator"};

// Только iterator и const_iterator (unordered контейнеры: unordered_map, unordered_set)
const std::vector<std::string> unordered_iterators = {"iterator", "const_iterator"};

// Конфигурация итераторов по паттернам
static const std::map<std::string, ContainerIteratorConfig>& container_iterator_configs() {
    static const std::map<std::string, ContainerIteratorConfig> configs{
        {"std::vector", {full_iterators}}, {"std::list", {full_iterators}}, {"std::deque", {full_iterators}},
        {"std::array", {full_iterators}},  {"std::*map", {full_iterators}}, {"std::*set", {full_iterators}},
    };
    return configs;
}

const ContainerIteratorConfig* get_container_iterator_config(const std::string& pattern) {
    const auto& configs = container_iterator_configs();
    auto it = configs.find(pattern);
    if (it != configs.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace trust
