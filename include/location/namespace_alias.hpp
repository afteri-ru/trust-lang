// Общее правило «префикс алиаса» для резолвера, реестра алиасов и LSP-проекций.
// Namespace-алиас — привязка префикса к области имён СВОЕГО/видимого модуля (`a :- ns::;`).
#pragma once

#include <string_view>

namespace trust {

/// Совпадение привязанного префикса с путём: `alias` — префикс `path` по границе `::`.
/// `rest` — остаток после `alias::` (пусто, если путь совпал ровно с алиасом). ЕДИНСТВЕННАЯ
/// точка правила «префикс алиаса» для резолвера, реестра алиасов и LSP-проекций.
[[nodiscard]] inline bool matchesAliasPrefix(std::string_view alias, std::string_view path, std::string_view* rest = nullptr) noexcept {
    if (alias.empty() || path.size() < alias.size() || !path.starts_with(alias)) {
        return false;
    }
    if (path.size() > alias.size() && !path.substr(alias.size()).starts_with("::")) {
        return false;
    }
    if (rest != nullptr) {
        *rest = path.size() == alias.size() ? std::string_view{} : path.substr(alias.size() + 2);
    }
    return true;
}

} // namespace trust
