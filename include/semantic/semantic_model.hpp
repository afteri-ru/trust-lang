// include/semantic/semantic_model.hpp
// Контракт выхода фазы семантики: persistent-таблица символов/скоупов + проекции для
// потребителей (LSP, будущие анализаторы). Модель строится ВСЕГДА при выполненном шаге
// семантики, а не по запросу: символы, видимость импорта, ссылки — часть результата прогона.
#pragma once

#include "location/module_index.hpp"
#include "types/type_system.hpp"
#include "symbols/reference_index.hpp"
#include "symbols/symbol_index.hpp"
#include "symbols/symbol_table.hpp"
#include "utils/string_interner.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace trust {

class AnalysisContext;

/// Данные ЗАГРУЗЧИКА, нужные потребителям модели (не семантика).
struct ModuleProjection {
    ModuleIndex mainModuleIndex{};
    std::vector<std::string> moduleNames;                           ///< индекс = ModuleIndex
    std::vector<std::pair<std::string, ModuleIndex>> moduleAliases; ///< `\m :- \lib;`
};

/// Неизменяемая после построения модель семантики прогона.
///
/// Владеет `AnalysisContext` ЦЕЛИКОМ (SymbolTable + persistent ScopeTree + NamespaceScope +
/// TypeResolution + ExprFacts + AnalysisState): перемещать `SymbolTable` отдельно нельзя —
/// зоны контекста держат ссылки на неё. Проекции вычисляются в конструкторе и хранятся без
/// копий из таблицы (журнал/поверхность/реестр алиасов читаются один раз).
///
/// ⚠ Владение типовой системой: модель держит `shared_ptr<const TypeSystem>` (SymbolTable
/// ссылается на неё НЕвладеюще). Читать типы из модели — `typeSystem().read()`.
class SemanticModel {
  public:
    SemanticModel(std::unique_ptr<AnalysisContext> analysis, std::shared_ptr<StringInterner> strings, std::shared_ptr<const TypeSystem> types,
                  ModuleProjection modules, ReferenceIndex references);
    ~SemanticModel();
    SemanticModel(SemanticModel&&) noexcept;
    SemanticModel& operator=(SemanticModel&&) noexcept;
    SemanticModel(const SemanticModel&) = delete;
    SemanticModel& operator=(const SemanticModel&) = delete;

    /// Persistent-таблица символов: локальные скоупы доступны после прогона.
    [[nodiscard]] const SymbolTable& table() const;
    /// Индекс объявлений (проекция собирается SymbolCollectorHook во время обхода).
    [[nodiscard]] const SymbolIndex& symbols() const;
    [[nodiscard]] const ReferenceIndex& references() const;

    /// Проекции главного модуля (импорт не транзитивен).
    [[nodiscard]] const std::vector<ImportSite>& importSites() const { return m_importSites; }
    [[nodiscard]] const std::vector<ReExportedNameInfo>& reExports() const { return m_reExports; }
    [[nodiscard]] const std::vector<UsingScopeInfo>& usingScopes() const { return m_usingScopes; }
    [[nodiscard]] const std::vector<NamespaceAliasInfo>& namespaceAliases() const { return m_namespaceAliases; }
    [[nodiscard]] const std::vector<NamespaceAliasInfo>& exportedNamespaceAliases() const { return m_exportedNamespaceAliases; }

    [[nodiscard]] ModuleIndex mainModuleIndex() const { return m_modules.mainModuleIndex; }
    [[nodiscard]] const ModuleProjection& modules() const { return m_modules; }
    /// Типовая система (владелец identity `TypeId`); read-view — `typeSystem().read()`.
    [[nodiscard]] const TypeSystem& typeSystem() const { return *m_types; }
    /// Пул строк компиляции, удерживаемый моделью (тексты валидны после разрушения `Context`).
    [[nodiscard]] const std::shared_ptr<StringInterner>& strings() const { return m_strings; }

    // -- Запросы к persistent-таблице (анализаторы/LSP) -----------------------------------
    /// Узел дерева скоупов, содержащий позицию (самый внутренний из тех, чей creator имеет
    /// ВАЛИДНЫЙ range; 0 — глобальный/не определено). Синтетические узлы без диапазона
    /// пропускаются: для точного ответа используйте `lookupAt` от известного скоупа.
    [[nodiscard]] std::size_t scopeAt(MapperRange point) const;
    /// Лексический поиск имени от узла скоупа (включая закрытые родительские узлы).
    [[nodiscard]] const Symbol* lookupAt(std::size_t scopeNode, const StorageKey& key) const;
    /// Символ объявления (представитель или одна из перегрузок); nullptr — не найден.
    [[nodiscard]] const Symbol* symbolOf(const AstNodeBase* decl) const;

  private:
    std::unique_ptr<AnalysisContext> m_analysis;
    std::shared_ptr<const TypeSystem> m_types; ///< TypeId модели переживают Context/Pipeline
    std::shared_ptr<StringInterner> m_strings; ///< тексты модели переживают Context
    ModuleProjection m_modules;
    ReferenceIndex m_references;
    std::vector<ImportSite> m_importSites;
    std::vector<ReExportedNameInfo> m_reExports;
    std::vector<UsingScopeInfo> m_usingScopes;
    std::vector<NamespaceAliasInfo> m_namespaceAliases;
    std::vector<NamespaceAliasInfo> m_exportedNamespaceAliases;
};

} // namespace trust
