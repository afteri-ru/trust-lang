#include "module_loader/interface/interface_install.hpp"

#include "diag/diag.hpp"
#include "module_loader/module_export.hpp"
#include "module_loader/module_loader.hpp"
#include "session/context.hpp"
#include "syntax/parser.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace trust {

void stampModuleLevelNode(AstNodeBase& node, ModuleIndex module, AstNodeBase::Visibility inherited) {
    node.module = module;
    if (node.kind() != ParserToken::Kind::ScopeBlock) {
        if (node.visibility == AstNodeBase::Visibility::Public) {
            node.visibility = inherited;
        }
        return;
    }
    auto& block = static_cast<ScopeBlock&>(node);
    // Блок интерфейса файла выглядит «анонимным» (пустой text после маскирования заголовка),
    // но его объявления — МОДУЛЬНОГО уровня (экспорт-набор файла).
    if (block.is_anonymous() && !block.isModuleInterface()) {
        return; // `{ ... }` — кодовый блок (операторы/локальные), не области модульного уровня
    }
    AstNodeBase::Visibility childVisibility = inherited;
    if (inherited == AstNodeBase::Visibility::Private || block.is_private()) {
        childVisibility = AstNodeBase::Visibility::Private;
    } else if (block.is_protected()) {
        childVisibility = AstNodeBase::Visibility::Protected;
    }
    for (AstNodePtr& child : block.m_body) {
        if (child) {
            stampModuleLevelNode(*child, module, childVisibility);
        }
    }
}

namespace {

/// Скрытые области (`_::`/`_{...}`) запрещены на любой глубине; загрузка-реэкспорт — только
/// на верхнем уровне блока (вложенный namespace-импорт не имеет семантики qualified-реэкспорта).
void checkInterfaceBody(Context& ctx, const std::vector<AstNodePtr>& body, bool nested) {
    for (const AstNodePtr& node : body) {
        if (!node) {
            continue;
        }
        if (node->kind() == ParserToken::Kind::ModuleDecl && nested) {
            const auto& mn = static_cast<const ModuleNode&>(*node);
            if (mn.isImport()) {
                ctx.diag().report(Severity::Error, node->range(), "re-export import is allowed only at the top level of the file interface");
            }
            continue;
        }
        if (node->kind() != ParserToken::Kind::ScopeBlock) {
            continue;
        }
        const auto& block = static_cast<const ScopeBlock&>(*node);
        if (block.is_hidden()) {
            const std::string_view spelling = block.is_private() ? "__::" : "_::";
            ctx.diag().report(Severity::Error, node->range(), "non-public block ('{}') is not allowed in the file interface", spelling);
            continue;
        }
        checkInterfaceBody(ctx, block.m_body, /*nested=*/true);
    }
}

/// Блок интерфейса — единственный ScopeBlock, начинающийся со `{` заголовка.
ScopeBlock* locateInterfaceBlock(Context& ctx, std::vector<AstNodePtr>& body, std::size_t interfaceBegin) {
    ScopeBlock* block = nullptr;
    for (std::size_t k = 0; k < body.size(); ++k) {
        const AstNodePtr& node = body[k];
        const bool startsInterface = node && !node->range().begin.isInvalid() && node->range().begin.offset() == interfaceBegin;
        if (!startsInterface) {
            continue;
        }
        if (block != nullptr || node->kind() != ParserToken::Kind::ScopeBlock) {
            ctx.diag().report(Severity::Error, node->range(), "internal: malformed file interface block");
            return nullptr;
        }
        block = static_cast<ScopeBlock*>(node.get());
    }
    if (block == nullptr) {
        ctx.diag().report(Severity::Error, MapperRange{}, "internal: file interface block was not parsed");
    }
    return block;
}

} // namespace

namespace {

/// Реэкспорт: сайт загрузки ВНУТРИ блока интерфейса публикует отфильтрованный масками набор имён
/// владельцев в экспорт-наборе ЭТОГО файла (владелец имён не меняется).
void markReExports(ModuleLoader& loader, const ScopeBlock& block, std::vector<ExportEntry>& reExportedEntries,
                   std::unordered_set<const AstNodeBase*>& reExportedNodes) {
    for (const AstNodePtr& child : block.m_body) {
        if (!child || child->kind() != ParserToken::Kind::ModuleDecl) {
            continue;
        }
        auto& mn = static_cast<ModuleNode&>(*child);
        if (!mn.isImport()) {
            continue;
        }
        mn.setReExport();
        for (const ModuleIndex part : mn.moduleIndices()) {
            for (const ExportEntry* entry : selectExportEntries(loader.exportEntries(part), mn.importMaskSet())) {
                if (!entry->node) {
                    continue;
                }
                if (reExportedNodes.insert(entry->node.get()).second) {
                    reExportedEntries.push_back(*entry);
                }
            }
        }
    }
}

/// Собственные объявления блока интерфейса; их qualified-имена — экспорт-набор файла.
std::vector<ExportEntry> collectOwnExports(const ModuleBodyScan& scan, const AstNodeBase* interfaceBlock, std::unordered_set<std::string>& exportedNames) {
    std::vector<ExportEntry> ownExports;
    ownExports.reserve(scan.entries.size());
    exportedNames.reserve(scan.entries.size());
    for (const ExportEntry& entry : scan.entries) {
        if (entry.container == interfaceBlock) {
            exportedNames.insert(entry.qualifiedName);
            ownExports.push_back(entry);
        }
    }
    return ownExports;
}

/// Определение record из тела за forward'ом интерфейса — реализация экспортного имени, НЕ входящая
/// в экспортную поверхность (импортёр видит forward; ключи регистрации не меняются).
void markExportImplementations(const ModuleBodyScan& scan, const AstNodeBase* interfaceBlock, const std::unordered_set<std::string>& exportedNames) {
    for (const ExportEntry& entry : scan.entries) {
        if (!entry.node || entry.container == interfaceBlock || !exportedNames.contains(entry.qualifiedName)) {
            continue;
        }
        if (isRecordTypeDecl(entry.node, /*wantBody=*/true)) {
            entry.node->isExportImplementation = true;
        }
    }
}

/// Объявления интерфейса — публичный контракт файла; чужие (реэкспорт) узлы не мутируются:
/// их приватность принадлежит модулю-владельцу.
void markExportedPublic(const std::vector<ExportEntry>& exports, const std::unordered_set<const AstNodeBase*>& reExportedNodes) {
    for (const ExportEntry& entry : exports) {
        if (entry.node && reExportedNodes.count(entry.node.get()) == 0) {
            entry.node->visibility = AstNodeBase::Visibility::Public;
        }
    }
}

/// Объявления ВНЕ интерфейса файл-приватны: имя не входит в экспорт-набор, регистрируется под
/// module-qualified ключом и эмитится в anonymous namespace. Entry-функция исключена (её C++-имя
/// связывает обёртка `_main.cppt`), protected-объявление приватизации не подлежит.
void markNonExportedPrivate(const ModuleBodyScan& scan, const std::unordered_set<std::string>& exportedNames) {
    for (const ExportEntry& entry : scan.entries) {
        if (!entry.node || entry.qualifiedName.ends_with("__main__")) {
            continue;
        }
        if (entry.visibility == AstNodeBase::Visibility::Protected) {
            continue;
        }
        if (exportedNames.find(entry.qualifiedName) != exportedNames.end()) {
            continue;
        }
        entry.node->visibility = AstNodeBase::Visibility::Private;
        // TypeDecl-узел — Binary (`Name ::= <RecordDecl/ClassDecl>`); семантика биндит имя по
        // ДОЧЕРНЕМУ узлу (`Symbol::decl`), поэтому признак нужен и на нём.
        if (entry.node->is<Binary>()) {
            Binary& decl = *entry.node->as<Binary>();
            if (decl.m_right) {
                decl.m_right->visibility = AstNodeBase::Visibility::Private;
            }
        }
    }
}

/// «Определено в этом файле»: тело функции либо инициализатор переменной.
std::unordered_set<std::string> collectDefinedNames(const ModuleBodyScan& scan) {
    std::unordered_set<std::string> definedNames;
    for (const ExportEntry& entry : scan.entries) {
        if (!entry.node) {
            continue;
        }
        const bool defines = (entry.node->is<FuncDecl>() && entry.node->as<FuncDecl>()->hasBody()) ||
                             (entry.node->is<VarDecl>() && entry.node->as<VarDecl>()->m_initializer != nullptr);
        if (defines) {
            definedNames.insert(entry.qualifiedName);
        }
    }
    return definedNames;
}

/// Каждое объявленное в интерфейсе имя обязано быть определено в этом же файле; реэкспорт и
/// алиасы относятся к другому файлу, native-объявления реализованы вне него.
void validateDeclaredOnly(Context& ctx, const std::vector<ExportEntry>& exports, const std::unordered_set<const AstNodeBase*>& reExportedNodes,
                          const std::unordered_set<std::string>& definedNames) {
    for (const ExportEntry& entry : exports) {
        if (!entry.node || reExportedNodes.count(entry.node.get()) != 0 || entry.qualifiedName.ends_with("__main__")) {
            continue;
        }
        if (entry.node->is<FuncDecl>() && entry.node->as<FuncDecl>()->aliasKind() != AliasKind::None) {
            continue;
        }
        const bool declaredOnly = entry.node->is<FuncDecl>() ? !entry.node->as<FuncDecl>()->hasBody()
                                                             : entry.node->is<VarDecl>() && !entry.node->as<VarDecl>()->m_initializer;
        if (!declaredOnly || definedNames.contains(entry.qualifiedName)) {
            continue;
        }
        if (entry.node->is<FuncDecl>()) {
            const FuncDecl& fn = *entry.node->as<FuncDecl>();
            if (fn.isNativeImport()) {
                continue;
            }
        }
        ctx.diag().report(Severity::Error, entry.node->range(), "'{}' is declared in the file interface but not defined in this file", entry.qualifiedName);
    }
}
}

// Интерфейс файла (`\path { ... };`): путь заголовка маскируется, блок разбирается обычной
// грамматикой и остаётся в AST одним ScopeBlock, начинающимся с `{` заголовка.
// Экспорт-набор файла = объявления этого блока; каждое объявленное в нём имя обязано быть
// определено в этом же файле (иначе интерфейс описывает реализацию, которой нет).
void installFileInterface(Context& ctx, ModuleLoader& loader, ModuleIndex idx, const ModuleHeaderScan& header, std::vector<AstNodePtr>& body) {
    // Сканер заголовка отдаёт 0-based смещение `{`, диапазоны узлов - 1-based.
    const std::size_t interfaceBegin = header.interfaceBegin + 1;
    ScopeBlock* block = locateInterfaceBlock(ctx, body, interfaceBegin);
    if (block == nullptr) {
        return;
    }
    block->setModuleInterface();
    checkInterfaceBody(ctx, block->m_body, /*nested=*/false);
    // ОДИН обход тела на все производные факты: экспорт-набор, определения, алиасы, приватность.
    const ModuleBodyScan scan = scanModuleBody(body);
    for (const AstNodeBase* aliasNode : scan.nonExportableNamespaceAliases) {
        if (aliasNode != nullptr) {
            const auto& aliasDecl = *static_cast<const FuncDecl*>(aliasNode);
            ctx.diag().report(Severity::Warning, aliasDecl.range(),
                              "namespace alias '{}' targets a namespace area and is not exported to importers", aliasDecl.text());
        }
    }
    std::vector<ExportEntry> reExportedEntries;
    std::unordered_set<const AstNodeBase*> reExportedNodes;
    markReExports(loader, *block, reExportedEntries, reExportedNodes);

    std::unordered_set<std::string> exportedNames;
    const std::vector<ExportEntry> ownExports = collectOwnExports(scan, scan.interfaceBlock, exportedNames);
    markExportImplementations(scan, scan.interfaceBlock, exportedNames);

    std::vector<ExportEntry> exports = ownExports;
    exports.insert(exports.end(), reExportedEntries.begin(), reExportedEntries.end());
    markExportedPublic(exports, reExportedNodes);
    markNonExportedPrivate(scan, exportedNames);

    // Факты считаются один раз: экспорт-набор, определения функций, признак out-of-line.
    ModuleInterfaceFacts facts = collectModuleInterface(scan, ownExports, reExportedEntries);
    const std::unordered_set<std::string> definedNames = collectDefinedNames(scan);
    validateDeclaredOnly(ctx, exports, reExportedNodes, definedNames);
    loader.setInterfaceFacts(idx, std::move(facts));
}

} // namespace trust
