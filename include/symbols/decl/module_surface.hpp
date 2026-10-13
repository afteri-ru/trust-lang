#pragma once

// include/symbols/decl/module_surface.hpp
// Единственное хранилище экспорт-поверхности модуля: объявления, зарегистрированные в корне
// модуля, и реэкспорт имён владельцев фасадами. Запись — `ExportRecord` (ключ хранения +
// символ), текстовых копий имени нет: все проекции выводятся из ключа.

#include "symbols/table/symbol.hpp"
#include "symbols/symbol_index.hpp"
#include "location/module_index.hpp"
#include "types/names.hpp"

#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {

class AstNodeBase;

/// Запись поверхности: ключ регистрации (идентичность модуля-владельца) + символ. Ключ —
/// единственный источник namespace-пути/имени (второй копии текста нет). Символ живёт в
/// persistent-дереве скоупов (`ScopeStack`), поэтому поверхность хранит только невладеющий
/// указатель: набор перегрузок пополняется в том же объекте и сразу виден поверхности.
struct ExportRecord {
    StorageKey key;
    Symbol* symbol = nullptr;

    bool operator==(const ExportRecord&) const = default;
};

/// Реэкспортированное имя: владелец и ЕГО ключ. Символ не копируется — читается из
/// `declarationsOf(owner)`, поэтому завершение forward/дополнение набора перегрузок у владельца
/// сразу видно через фасад (один факт — одно хранилище).
struct ReExportRef {
    ModuleIndex owner{};
    StorageKey key;

    bool operator==(const ReExportRef&) const = default;
};

class ModuleSurface {
  public:
    /// Записать/обновить объявление в поверхности модуля. Указатель на символ берётся из
    /// persistent-скоупа (живой объект): при совпадении ключа побеждает запись с более полным
    /// набором перегрузок, иначе — входящая.
    void recordDeclaration(ModuleIndex owner, const ExportRecord& record);
    /// Реэкспорт: имена владельца `owner`, чьи узлы-объявления входят в `exportedNodes`,
    /// публикуются фасадом `facade` (транзитивно, включая реэкспорт владельца).
    void recordReExport(ModuleIndex facade, ModuleIndex owner, const std::vector<const AstNodeBase*>& exportedNodes);

    [[nodiscard]] std::span<const ExportRecord> declarationsOf(ModuleIndex owner) const;
    /// Реэкспортированные имена фасада, разрешённые в записи владельцев (порядок публикации).
    [[nodiscard]] std::vector<ExportRecord> reExportsOf(ModuleIndex facade) const;
    /// Есть ли у модуля объявления или реэкспорт (иначе резолв модуля не нужен).
    [[nodiscard]] bool hasModule(ModuleIndex module) const;
    [[nodiscard]] bool hasNamespace(ModuleIndex module, std::string_view path) const;

    /// Объявление поверхности по qualified-имени внутри модуля (без текстовой склейки).
    [[nodiscard]] const ExportRecord* findDeclaration(ModuleIndex module, std::string_view namespacePath, std::string_view name) const;
    /// Реэкспортированное имя владельца, опубликованное фасадом.
    [[nodiscard]] const ExportRecord* findReExport(ModuleIndex facade, std::string_view namespacePath, std::string_view name) const;

    /// Проекция для LSP: детерминированный порядок (facade, owner, namespacePath, name).
    [[nodiscard]] std::vector<ReExportedNameInfo> reExportProjection() const;

  private:
    /// Ссылки реэкспорта фасада (nullptr — записей нет).
    [[nodiscard]] const std::vector<ReExportRef>* reExportRefsOf(ModuleIndex facade) const;
    /// Запись владельца, на которую ссылается реэкспорт (nullptr — владелец не публиковал имя).
    [[nodiscard]] const ExportRecord* resolveReExport(const ReExportRef& ref) const;

    std::unordered_map<ModuleIndex, std::vector<ExportRecord>> m_declarations;
    std::unordered_map<ModuleIndex, std::vector<ReExportRef>> m_reExported;
};

} // namespace trust
