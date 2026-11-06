// Сбор экспорт-интерфейса модуля.
#include "module_loader/module_export.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "syntax/term/term.h"
#include "types/names.hpp"
#include "utils/import_masks.hpp"
#include "utils/strings.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace trust {

namespace {

/// Признак out-of-line определений в поддереве (реализация ниже).
bool subtreeHasDefinitions(const std::vector<AstNodePtr>& roots);

/// Является ли AST-узел экспортируемым объявлением (топ-левел декларация).
bool isExportDeclKind(ParserToken::Kind kind) {
    switch (kind) {
    case ParserToken::Kind::VarDecl:
    case ParserToken::Kind::FuncDecl:
    case ParserToken::Kind::TypeDecl:
    case ParserToken::Kind::StructDecl:
    case ParserToken::Kind::EnumDecl:
        return true;
    default:
        return false;
    }
}

/// Имя экспортируемой декларации. Для TypeDecl (`::=`) имя - слева (Ident); для
/// VarDecl/FuncDecl/StructDecl/EnumDecl - текст узла (маски фильтра соответствуют логическому имени).
std::string_view declNameOf(const AstNodeBase& node) {
    std::string_view name;
    if (node.kind() == ParserToken::Kind::TypeDecl) {
        const auto& b = static_cast<const Binary&>(node);
        // Имя типа слева: `Name` (Ident) либо `:Name` (TypeName) — оба варианта допустимы.
        if (b.m_left && (b.m_left->kind() == ParserToken::Kind::Ident || b.m_left->kind() == ParserToken::Kind::TypeName)) {
            name = requiredText(*b.m_left);
        } else {
            return {};
        }
    } else {
        name = requiredText(node);
    }
    if (!name.empty() && name.front() == ':') {
        name.remove_prefix(1);
    }
    return name;
}

/// Namespace-путь из текста ScopeBlock (`ns::`/`::ns::`/`ns::sub::`) относительно родителя.
std::string extendPrefix(std::string_view prefix, std::string_view blockText) {
    std::string_view text = blockText;
    if (text.rfind("::", 0) == 0) {
        text.remove_prefix(2);
    }
    if (text.size() >= 2 && text.substr(text.size() - 2) == "::") {
        text.remove_suffix(2);
    }
    if (text.empty()) {
        return std::string(prefix);
    }
    return utils::qualifyTrustName(prefix, text);
}

/// Рекурсивный ЕДИНСТВЕННЫЙ обход тела модуля: экспорт-записи (с происхождением) и
/// экспортируемые namespace-алиасы блока интерфейса. Контейнеры SEQUENCE уже развёрнуты;
/// пользовательские `{ ... }` сохраняются как ScopeBlock. Скоупы с анонимной (`_`) областью и
/// безымянные кодовые блоки пропускаются целиком; именованные/глобальные области обходятся
/// рекурсивно с накоплением namespace-пути. Корневой `ModuleNode` разворачивается: модуль - не
/// namespace-сегмент. `container` — top-level узел, из которого пришла запись (для разбиения
/// экспорт-набора по блоку интерфейса без повторного обхода).
void scanRec(const std::vector<AstNodePtr>& body, std::string_view prefix, AstNodeBase::Visibility visibility, const AstNodeBase* container, bool inInterface,
             ModuleBodyScan& scan) {
    for (const AstNodePtr& node : body) {
        if (!node) {
            continue;
        }
        const AstNodeBase* nodeContainer = container != nullptr ? container : node.get();
        // Корневой модуль (не сайт импорта): тело - это и есть содержимое единицы текста.
        if (node->kind() == ParserToken::Kind::ModuleDecl) {
            const auto& mn = static_cast<const ModuleNode&>(*node);
            if (!mn.isImport()) {
                scanRec(mn.m_body, prefix, visibility, nodeContainer, inInterface, scan);
                continue;
            }
        }
        if (node->is<FuncDecl>()) {
            const auto& fn = *node->as<FuncDecl>();
            // Namespace-алиас (`a :- ns::;`) — привязка префикса в скоупе, а не имя: в экспорт-набор
            // не входит. Цель — область имён без модульной квалификации, реэкспорт невыразим:
            // предупреждение в `interface_install` (у сканера нет Context).
            if (fn.aliasKind() == AliasKind::Namespace) {
                if (inInterface) {
                    scan.nonExportableNamespaceAliases.push_back(node.get());
                }
                continue;
            }
        }
        if (isExportDeclKind(node->kind())) {
            const std::string_view name = declNameOf(*node);
            if (!name.empty()) {
                scan.entries.push_back(ExportEntry{node, utils::qualifyTrustName(prefix, name), visibility, nodeContainer});
            }
            // Out-of-line определения: тело функции/инициализатор переменной. Тела методов внутри
            // record и определения в скрытых блоках сюда не попадают (обход их пропускает) —
            // они считаются поддеревом явно.
            if (!scan.hasOutOfLineDefinitions) {
                if (node->is<FuncDecl>()) {
                    scan.hasOutOfLineDefinitions = node->as<FuncDecl>()->hasBody();
                } else if (node->is<VarDecl>()) {
                    scan.hasOutOfLineDefinitions = node->as<VarDecl>()->m_initializer != nullptr;
                } else {
                    scan.hasOutOfLineDefinitions = subtreeHasDefinitions({node});
                }
            }
            continue;
        }
        // Области имён (ScopeBlock). Анонимная `_` и безымянный кодовый блок - пропускаем.
        if (node->as_sequence() != nullptr && node->kind() == ParserToken::Kind::ScopeBlock) {
            const ScopeBlock& sb = static_cast<const ScopeBlock&>(*node);
            // Блок интерфейса файла - container уровня модуля: объявления экспортируются без
            // namespace-префикса (эквивалент `::`), а сам блок становится происхождением записей.
            if (sb.isModuleInterface()) {
                if (scan.interfaceBlock == nullptr) {
                    scan.interfaceBlock = node.get();
                }
                scanRec(sb.m_body, prefix, visibility, node.get(), /*inInterface=*/true, scan);
                continue;
            }
            if (sb.is_private() || sb.is_anonymous()) {
                // Скрытый/кодовый блок в экспорт-набор не входит, но его определения требуют
                // собственного `.cppt` (эмитятся только в TU владельца).
                if (!scan.hasOutOfLineDefinitions) {
                    scan.hasOutOfLineDefinitions = subtreeHasDefinitions(sb.m_body);
                }
                continue;
            }
            const AstNodeBase::Visibility childVisibility = sb.is_protected() ? AstNodeBase::Visibility::Protected : visibility;
            const std::string_view text = sb.text();
            // Глобальная область `::` - содержимое без изменения пути; `_::` - module-level
            // protected без namespace-сегмента.
            if (sb.is_protected()) {
                scanRec(sb.m_body, prefix, childVisibility, nodeContainer, inInterface, scan);
            } else {
                scanRec(sb.m_body, text == "::" ? prefix : extendPrefix(prefix, text), childVisibility, nodeContainer, inInterface, scan);
            }
        }
    }
}

} // namespace

/// Эквивалент `findFunctionDefinition`: определение функции по qualified-имени и C++-суффиксу.
AstNodePtr ModuleInterfaceFacts::findFunctionDefinition(std::string_view qualifiedName, std::string_view cppSuffix,
                                                        const FuncCppSuffixProvider& suffixOf) const {
    const auto found = functionDefinitions.find(std::string(qualifiedName));
    if (found == functionDefinitions.end()) {
        return nullptr;
    }
    for (const AstNodePtr& candidate : found->second) {
        if (candidate && candidate->is<FuncDecl>() && suffixOf(*candidate->as<FuncDecl>()) == cppSuffix) {
            return candidate;
        }
    }
    return nullptr;
}

namespace {

/// `TypeDecl` (`::=`/`=`) с RHS-RecordDecl: forward либо определение (см. ниже).
/// Есть ли в поддереве определения, требующие собственного `.cppt`: тело функции, инициализатор
/// переменной или тело метода внутри record. Сайты импорта пропускаются (чужое тело).
bool subtreeHasDefinitions(const std::vector<AstNodePtr>& roots) {
    std::vector<AstNodePtr> stack(roots.begin(), roots.end());
    while (!stack.empty()) {
        AstNodePtr node = std::move(stack.back());
        stack.pop_back();
        if (!node) {
            continue;
        }
        if (node->kind() == ParserToken::Kind::ModuleDecl) {
            const auto& mn = static_cast<const ModuleNode&>(*node);
            if (mn.isImport()) {
                continue;
            }
            stack.insert(stack.end(), mn.m_body.begin(), mn.m_body.end());
            continue;
        }
        if (node->is<FuncDecl>()) {
            if (node->as<FuncDecl>()->hasBody()) {
                return true;
            }
            continue;
        }
        if (node->is<VarDecl>()) {
            if (node->as<VarDecl>()->m_initializer != nullptr) {
                return true;
            }
            continue;
        }
        // Области имён, тело record'а и РЕАЛИЗАЦИЯ класса (методы с телами внутри ClassDecl
        // `Name = %cpp { ... }`) содержат определения того же модуля. Тело ClassDecl/RecordDecl
        // хранится отдельно (m_body/Definition), а не в children().
        if (node->kind() == ParserToken::Kind::ScopeBlock) {
            for (const AstNodePtr& child : node->children()) {
                stack.push_back(child);
            }
            continue;
        }
        if (node->kind() == ParserToken::Kind::TypeDecl && node->is<Binary>()) {
            // `Name = %cpp { ... }`: тело реализации - в RHS ClassDecl; сам TypeDecl - Binary.
            if (const AstNodePtr& rhs = node->as<Binary>()->m_right) {
                stack.push_back(rhs);
            }
            continue;
        }
        if (node->is<RecordDecl>() || node->is<ClassDecl>()) {
            // Только ФУНКЦИИ-члены: поле с инициализатором (NSDMI) — не out-of-line определение модуля.
            const std::vector<AstNodePtr>* members = node->is<RecordDecl>() ? node->as<RecordDecl>()->body() : &node->as<ClassDecl>()->m_body;
            if (members != nullptr) {
                for (const AstNodePtr& member : *members) {
                    if (member && member->is<FuncDecl>()) {
                        stack.push_back(member);
                    }
                }
            }
        }
    }
    return false;
}

} // namespace

ModuleBodyScan scanModuleBody(const std::vector<AstNodePtr>& roots) {
    ModuleBodyScan scan;
    scanRec(roots, /*prefix=*/{}, AstNodeBase::Visibility::Public, /*container=*/nullptr, /*inInterface=*/false, scan);
    // Определение record, объявленного forward'ом в интерфейсе файла, не входит в экспорт-набор
    // (сайт импорта видит только forward), поэтому эмитится ТОЛЬКО в TU владельца: такому модулю
    // нужен `.cppt` и без out-of-line функций/переменных. Forward-имена — из того же обхода.
    if (scan.hasOutOfLineDefinitions || scan.interfaceBlock == nullptr) {
        return scan;
    }
    std::unordered_set<std::string> interfaceForwards;
    for (const ExportEntry& entry : scan.entries) {
        if (entry.container == scan.interfaceBlock && isRecordTypeDecl(entry.node, /*wantBody=*/false)) {
            interfaceForwards.insert(entry.qualifiedName);
        }
    }
    for (const ExportEntry& entry : scan.entries) {
        if (interfaceForwards.contains(entry.qualifiedName) && isRecordTypeDecl(entry.node, /*wantBody=*/true)) {
            scan.hasOutOfLineDefinitions = true;
            break;
        }
    }
    return scan;
}

std::vector<const ExportEntry*> selectExportEntries(const std::vector<ExportEntry>& entries, const utils::ImportMasks& masks) {
    std::vector<const ExportEntry*> selected;
    selected.reserve(entries.size());
    for (const ExportEntry& entry : entries) {
        if (masks.matches(entry.qualifiedName)) {
            selected.push_back(&entry);
        }
    }
    return selected;
}

bool isRecordTypeDecl(const AstNodePtr& node, bool wantBody) {
    if (!node || node->kind() != ParserToken::Kind::TypeDecl || !node->is<Binary>()) {
        return false;
    }
    const auto& b = static_cast<const Binary&>(*node);
    if (!b.m_right || b.m_right->kind() != ParserToken::Kind::StructDecl) {
        return false;
    }
    return static_cast<const RecordDecl&>(*b.m_right).hasBody() == wantBody;
}

ModuleInterfaceFacts collectModuleInterface(const std::vector<AstNodePtr>& roots, const std::vector<ExportEntry>& interfaceExports,
                                            const std::vector<ExportEntry>& reExported) {
    return collectModuleInterface(scanModuleBody(roots), interfaceExports, reExported);
}

ModuleInterfaceFacts collectModuleInterface(const ModuleBodyScan& scan, const std::vector<ExportEntry>& interfaceExports,
                                            const std::vector<ExportEntry>& reExported) {
    ModuleInterfaceFacts facts;
    // Блок интерфейса — факт того же обхода тела (повторного поиска по `roots` нет).
    if (scan.interfaceBlock != nullptr) {
        facts.entries = interfaceExports;
        facts.entries.insert(facts.entries.end(), reExported.begin(), reExported.end());
        for (const ExportEntry& entry : scan.entries) {
            if (entry.visibility == AstNodeBase::Visibility::Protected) {
                facts.protectedEntries.push_back(entry);
            }
        }
    } else {
        for (const ExportEntry& entry : scan.entries) {
            if (entry.visibility == AstNodeBase::Visibility::Protected) {
                facts.protectedEntries.push_back(entry);
            } else {
                facts.entries.push_back(entry);
            }
        }
    }

    for (const ExportEntry& entry : scan.entries) {
        if (!entry.node) {
            continue;
        }
        if (entry.node->is<FuncDecl>()) {
            const FuncDecl& fn = *entry.node->as<FuncDecl>();
            if (fn.hasBody()) {
                facts.functionDefinitions[entry.qualifiedName].push_back(entry.node);
            }
        }
    }

    facts.hasOutOfLineDefinitions = scan.hasOutOfLineDefinitions;
    return facts;
}

std::vector<AstNodePtr> filterExportEntries(const std::vector<ExportEntry>& entries, const utils::ImportMasks& masks,
                                            std::vector<std::string>* unmatchedMasks) {
    std::vector<const ExportEntry*> selected = selectExportEntries(entries, masks);
    std::vector<AstNodePtr> out;
    out.reserve(selected.size());
    for (const ExportEntry* entry : selected) {
        out.push_back(entry->node);
    }

    if (unmatchedMasks != nullptr && !masks.empty()) {
        // Имена — views на уже посчитанные qualified-имена (копий строк нет).
        std::vector<std::string_view> names;
        names.reserve(entries.size());
        for (const ExportEntry& entry : entries) {
            names.push_back(entry.qualifiedName);
        }
        *unmatchedMasks = masks.unmatchedIn(names);
    }
    return out;
}

} // namespace trust