#pragma once

// include/symbols/import/alias_registry.hpp
// Единственное хранилище using-областей и namespace-алиасов: записи лежат в реестре ОДИН раз,
// а лексический скоуп держит только индексы своих записей. Проекции для LSP/Pipeline
// вычисляются из реестра (копий нет). Правило резолва (longest-prefix, только алиасы своего
// модуля) остаётся за NameResolver/SymbolTable.

#include "symbols/table/scope_stack.hpp"
#include "symbols/symbol_index.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "location/namespace_alias.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace trust {

/// Область поиска `... = X` (using): путь-префикс и модуль-владелец. `operatorRange` — позиция
/// оператора (диагностика на выходе из скоупа), `scopeRange` — скоуп-создатель (проекция LSP).
struct UsingScopeRecord {
    ModuleIndex importer{};
    std::string_view path;
    std::optional<ModuleIndex> targetModule;
    MapperRange operatorRange;
    MapperRange scopeRange;
};

/// Происхождение namespace-алиаса: локальное объявление/привязка сайта, факт интерфейса
/// модуля (из загрузчика) либо перенос в корень при выходе из exported namespace.
enum class AliasOrigin : unsigned char { Local, ExportedFact, PromotedToRoot };

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
    /// Зарегистрировать using-область в скоупе (дедуп по (importer, target, path) внутри скоупа).
    void recordUsing(Scope& scope, UsingScopeRecord record);
    /// Привязать namespace-алиас в скоупе. false — префикс уже привязан к ДРУГОЙ цели.
    [[nodiscard]] bool recordAlias(Scope& scope, NamespaceAliasRecord record);
    /// Экспорт-факты интерфейса модуля (единственный писатель — сайт импорта/загрузчик).
    void recordExportedFacts(ModuleIndex owner, std::span<const ModuleNamespaceAlias> aliases);
    /// Перенести алиас в корневой скоуп (квалифицированное имя переживает выход из блока).
    [[nodiscard]] bool promoteAliasToRoot(Scope& rootScope, NamespaceAliasRecord record);

    [[nodiscard]] const UsingScopeRecord& usingAt(std::size_t ref) const noexcept { return m_using[ref]; }
    [[nodiscard]] const NamespaceAliasRecord& aliasAt(std::size_t ref) const noexcept { return m_aliases[ref]; }

    /// Есть ли у модуля экспортированные namespace-алиасы (факты интерфейса).
    [[nodiscard]] bool hasExportedFacts(ModuleIndex owner) const noexcept;
    /// Самый длинный экспортированный алиас модуля, совпавший с префиксом `nsPath`
    /// (longest-prefix). `rest` — остаток пути после алиаса. nullptr — совпадений нет.
    [[nodiscard]] const NamespaceAliasRecord* longestExportedAlias(ModuleIndex owner, std::string_view nsPath, std::string_view* rest) const;

    /// Проекции: единственный источник — записи реестра (копий не хранится).
    [[nodiscard]] std::vector<UsingScopeInfo> usingProjection() const;
    [[nodiscard]] std::vector<NamespaceAliasInfo> aliasProjection() const;
    [[nodiscard]] std::vector<NamespaceAliasInfo> exportedProjection() const;

  private:
    std::vector<UsingScopeRecord> m_using;
    std::vector<NamespaceAliasRecord> m_aliases;
};

} // namespace trust
