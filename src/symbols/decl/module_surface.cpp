#include "symbols/decl/module_surface.hpp"

#include "ast/ast_nodes.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"

#include <algorithm>
#include <tuple>
#include <unordered_set>

namespace trust {

namespace {

bool sameKeyName(const StorageKey& key, std::string_view namespacePath, std::string_view name) noexcept {
    return key.namespacePath == namespacePath && key.name == name;
}

bool lessReExportedName(const ReExportedNameInfo& a, const ReExportedNameInfo& b) {
    return std::tie(a.facade, a.owner, a.namespacePath, a.name) < std::tie(b.facade, b.owner, b.namespacePath, b.name);
}

/// Записать запись ровно один раз (порядок первого появления наблюдаем).
template <typename Record>
void appendIfAbsent(std::vector<Record>& target, const Record& source) {
    for (const Record& existing : target) {
        if (existing == source) {
            return;
        }
    }
    target.push_back(source);
}

} // namespace

void ModuleSurface::recordDeclaration(ModuleIndex owner, const ExportRecord& record) {
    EXPECT(record.symbol != nullptr && "ModuleSurface::recordDeclaration: record must carry a symbol");
    auto& list = m_declarations[owner];
    for (ExportRecord& existing : list) {
        if (!(existing.key == record.key)) {
            continue;
        }
        // Указатели обеих записей — на ЖИВЫЕ символы скоупов: более полный набор перегрузок
        // не должен затираться копией из другого скоупа (завершение forward меняет
        // представителя на определение в том же объекте).
        const std::size_t existingOverloads = existing.symbol != nullptr ? existing.symbol->overloads.size() : 0;
        const std::size_t incomingOverloads = record.symbol != nullptr ? record.symbol->overloads.size() : 0;
        if (incomingOverloads >= existingOverloads) {
            existing.symbol = record.symbol;
        }
        return;
    }
    list.push_back(record);
}

void ModuleSurface::recordReExport(ModuleIndex facade, ModuleIndex owner, const std::vector<const AstNodeBase*>& exportedNodes) {
    if (exportedNodes.empty()) {
        return;
    }
    const std::unordered_set<const AstNodeBase*> exported(exportedNodes.begin(), exportedNodes.end());
    // Публикация собирается ДО записи: реэкспорт без совпадений не должен создавать пустую запись
    // (иначе `hasModule` ложно истинен и привязка импорта идёт вхолостую).
    std::vector<ReExportRef> published;
    for (const ExportRecord& declaration : declarationsOf(owner)) {
        if (declaration.symbol == nullptr || declaration.symbol->decl == nullptr || exported.count(declaration.symbol->decl) == 0) {
            continue;
        }
        const ModuleIndex actualOwner = declaration.key.module.isValid() ? declaration.key.module : owner;
        appendIfAbsent(published, ReExportRef{actualOwner, declaration.key});
    }
    // Транзитивность: фасад публикует имена, полученные реэкспортом (владелец — исходный модуль).
    if (const std::vector<ReExportRef>* ownerRefs = reExportRefsOf(owner); ownerRefs != nullptr) {
        for (const ReExportRef& ref : *ownerRefs) {
            const ExportRecord& record = *resolveReExport(ref);
            if (record.symbol->decl == nullptr || exported.count(record.symbol->decl) == 0) {
                continue;
            }
            appendIfAbsent(published, ref);
        }
    }
    if (published.empty()) {
        return;
    }
    std::vector<ReExportRef>& target = m_reExported[facade];
    for (const ReExportRef& ref : published) {
        appendIfAbsent(target, ref);
    }
}

std::span<const ExportRecord> ModuleSurface::declarationsOf(ModuleIndex owner) const {
    const auto found = m_declarations.find(owner);
    if (found == m_declarations.end()) {
        return {};
    }
    return found->second;
}

const std::vector<ReExportRef>* ModuleSurface::reExportRefsOf(ModuleIndex facade) const {
    const auto found = m_reExported.find(facade);
    return found == m_reExported.end() ? nullptr : &found->second;
}

const ExportRecord* ModuleSurface::resolveReExport(const ReExportRef& ref) const {
    const ExportRecord* record = findDeclaration(ref.owner, ref.key.namespacePath, ref.key.name);
    // Записи владельца не удаляются: реэкспортная ссылка обязана разрешаться всегда.
    EXPECT(record != nullptr && "ModuleSurface::resolveReExport: re-export reference does not resolve");
    return record;
}

std::vector<ExportRecord> ModuleSurface::reExportsOf(ModuleIndex facade) const {
    std::vector<ExportRecord> out;
    const std::vector<ReExportRef>* refs = reExportRefsOf(facade);
    if (refs == nullptr) {
        return out;
    }
    out.reserve(refs->size());
    for (const ReExportRef& ref : *refs) {
        out.push_back(*resolveReExport(ref));
    }
    return out;
}

bool ModuleSurface::hasModule(ModuleIndex module) const {
    return m_declarations.find(module) != m_declarations.end() || m_reExported.find(module) != m_reExported.end();
}

bool ModuleSurface::hasNamespace(ModuleIndex module, std::string_view path) const {
    if (!module.isValid() || path.empty()) {
        return false;
    }
    // Область модуля существует и тогда, когда все её имена пришли реэкспортом: `m_reExported`
    // хранит ключи владельцев, поэтому проверяются оба набора.
    for (const ExportRecord& declaration : declarationsOf(module)) {
        if (declaration.key.namespacePath == path) {
            return true;
        }
    }
    if (const std::vector<ReExportRef>* refs = reExportRefsOf(module); refs != nullptr) {
        for (const ReExportRef& ref : *refs) {
            if (ref.key.namespacePath == path) {
                return true;
            }
        }
    }
    return false;
}

const ExportRecord* ModuleSurface::findDeclaration(ModuleIndex module, std::string_view namespacePath, std::string_view name) const {
    for (const ExportRecord& declaration : declarationsOf(module)) {
        if (sameKeyName(declaration.key, namespacePath, name)) {
            return &declaration;
        }
    }
    return nullptr;
}

const ExportRecord* ModuleSurface::findReExport(ModuleIndex facade, std::string_view namespacePath, std::string_view name) const {
    const std::vector<ReExportRef>* refs = reExportRefsOf(facade);
    if (refs == nullptr) {
        return nullptr;
    }
    for (const ReExportRef& ref : *refs) {
        if (sameKeyName(ref.key, namespacePath, name)) {
            return resolveReExport(ref);
        }
    }
    return nullptr;
}

std::vector<ReExportedNameInfo> ModuleSurface::reExportProjection() const {
    std::vector<ReExportedNameInfo> out;
    for (const auto& [facade, refs] : m_reExported) {
        for (const ReExportRef& ref : refs) {
            const ExportRecord& record = *resolveReExport(ref);
            // Проекция LSP адресует объявление: безымянные/вне-модульные записи не публикуются.
            if (record.symbol->decl == nullptr || !record.symbol->decl->module.isValid()) {
                continue;
            }
            out.push_back(ReExportedNameInfo{facade, ref.owner, std::string(ref.key.namespacePath), std::string(ref.key.name)});
        }
    }
    // Детерминированный порядок: m_reExported — unordered_map, а результат кэшируется в LSP.
    std::sort(out.begin(), out.end(), lessReExportedName);
    return out;
}

} // namespace trust
