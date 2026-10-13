// include/location/namespace_alias.hpp
// Namespace-алиас модуля: привязка префикса к области другого модуля. Не сущность —
// используется AST-сайтом импорта и фактами интерфейса; объявлен в нижнем слое,
// чтобы не нарушать зависимость ast → module_loader.
#pragma once

#include "location/module_index.hpp"

#include <string>
#include <string_view>

namespace trust {

struct ModuleNamespaceAlias {
    std::string alias;          ///< Префикс, под которым видна область.
    ModuleIndex targetModule{}; ///< Модуль-владелец области.
    std::string path;           ///< Путь области в модуле-владельце.
};

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
