#pragma once

// Единственное хранилище using-областей и namespace-алиасов: записи лежат в реестре ОДИН раз,
// а лексический скоуп держит только индексы своих записей. Проекции для LSP/Pipeline
// вычисляются из реестра (копий нет). Правило резолва (longest-prefix, только алиасы своего
// модуля) остаётся за NameResolver/SymbolTable.

#include "symbols/table/scope_stack.hpp"
#include "symbols/symbol_index.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace trust {

/// Область поиска `... = X` (using): путь-префикс области имён. `operatorRange` — позиция
/// оператора (диагностика на выходе из скоупа), `scopeRange` — скоуп-создатель (проекция LSP).
struct UsingScopeRecord {
    ModuleIndex importer{};
    std::string_view path;
    MapperRange operatorRange;
    MapperRange scopeRange;
};

/// Происхождение namespace-алиаса: локальное объявление/привязка сайта либо перенос в корень
/// при выходе из exported namespace.
enum class AliasOrigin : unsigned char { Local, PromotedToRoot };

struct NamespaceAliasRecord {
    ModuleIndex owner{};
    std::string_view alias;
    ModuleIndex targetModule{};
    std::string_view path;
    MapperRange scopeRange;
    AliasOrigin origin = AliasOrigin::Local;
};

class AliasRegistry {
  public:
    /// Зарегистрировать using-область в скоупе (дедуп по (importer, path) внутри скоупа).
    void recordUsing(Scope& scope, UsingScopeRecord record);
    /// Привязать namespace-алиас в скоупе. false — префикс уже привязан к ДРУГОЙ цели.
    [[nodiscard]] bool recordAlias(Scope& scope, NamespaceAliasRecord record);
    /// Перенести алиас в корневой скоуп (квалифицированное имя переживает выход из блока).
    [[nodiscard]] bool promoteAliasToRoot(Scope& rootScope, NamespaceAliasRecord record);

    [[nodiscard]] const UsingScopeRecord& usingAt(std::size_t ref) const noexcept { return m_using[ref]; }
    [[nodiscard]] const NamespaceAliasRecord& aliasAt(std::size_t ref) const noexcept { return m_aliases[ref]; }

    /// Проекции: единственный источник — записи реестра (копий не хранится).
    [[nodiscard]] std::vector<UsingScopeInfo> usingProjection() const;
    [[nodiscard]] std::vector<NamespaceAliasInfo> aliasProjection() const;

  private:
    std::vector<UsingScopeRecord> m_using;
    std::vector<NamespaceAliasRecord> m_aliases;
};

} // namespace trust
