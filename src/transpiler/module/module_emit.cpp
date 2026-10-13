#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/emit/decl_emit_common.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace trust {

void DeclEmitter::visit_ModuleDecl(const ModuleNode& n) {
    if (n.isImport()) {
        // Сайт импорта `\module(mod, masks)`: вместо полного тела - только `#include` заголовка
        // модуля и регистрация имя-зависимых фактов. Определения живут в `.cppt` владельца.
        emitModuleImportDecls(n);
        return;
    }
    // Корневой модуль (главный файл или модульный TU) - полное тело. Модуль юнита нужен
    // текстовым ссылкам без resolved-узла (атрибуты/CLI-имена) для модульной изоляции.
    const ModuleIndex previousModule = m_ectx.m_output.currentModule;
    m_ectx.m_output.currentModule = n.moduleIndex();
    // Однофайловый режим (-fsingle-file): если у модуля НЕТ entry-функции (`...__main__`),
    // top-level операторы модуля оборачиваются в синтезируемую entry («модуль-скрипт»); если
    // entry есть - эмитим как обычно (pipeline дописывает main-обёртку).
    if (m_ectx.m_unit.singleFileMode) {
        bool hasEntry = false;
        for (const auto& child : n.m_body) {
            if (child && child->kind() == ParserToken::Kind::FuncDecl) {
                const std::string t(requiredText(*child));
                if (!t.empty() && t.ends_with("__main__")) {
                    hasEntry = true;
                    break;
                }
            }
        }
        if (!hasEntry) {
            // Синтезируемая entry обязана быть вне namespace модуля. Изоляция корня здесь не нужна:
            // импорт корня образовал бы цикл, поэтому на его символы никто снаружи не ссылается.
            m_driver.emitSingleFileModuleBody(n, m_ectx.m_output.out);
            m_ectx.m_output.currentModule = previousModule;
            return;
        }
    }
    emitModuleBodyIsolated(n, m_ectx.m_output.out);
    m_ectx.m_output.currentModule = previousModule;
}

// Тело модульного юнита с автоматическим namespace модуля. Entry-функции (`__main__`) и
// top-level C++-вставки остаются ВНЕ конверта: entry связан с `_main.cppt` точным глобальным
// именем, а `{% %}` — произвольный C++ (может объявлять глобальные символы/вход).
namespace {

/// Узел верхнего уровня модуля, который НЕЛЬЗЯ помещать в автоматический namespace модуля:
/// entry (точное глобальное имя для `_main.cppt`), C++-вставки (произвольный C++, может объявлять
/// глобальные символы), нативные декларации (квалифицированный внешний символ нельзя объявлять в
/// чужом namespace) и сайты импорта (чужой модуль живёт в своём глобальном namespace).
bool staysOutsideModuleNamespace(const AstNodePtr& child) {
    if (!child) {
        return false;
    }
    if (child->kind() == ParserToken::Kind::EmbedExpr) {
        return true;
    }
    if (child->is<ModuleNode>()) {
        return static_cast<const ModuleNode&>(*child).isImport();
    }
    if (child->is<FuncDecl>()) {
        const auto& func = *child->as<FuncDecl>();
        if (func.text().ends_with("__main__")) {
            return true;
        }
        return func.m_isNativeImport || func.m_isNativeTemplateImport || func.m_isNativeTemplateCtor || (!func.text().empty() && func.text().front() == '%');
    }
    if (child->is<VarDecl>()) {
        const auto& var = *child->as<VarDecl>();
        return !var.text().empty() && var.text().front() == '%';
    }
    return false;
}

} // namespace

void DeclEmitter::emitModuleBodyIsolated(const ModuleNode& n, MapperFile out) {
    const std::string moduleNamespace = m_ectx.moduleNamespace(n.moduleIndex());
    if (moduleNamespace.empty()) {
        m_driver.emitSequenceBody(n, out);
        return;
    }
    // Порядок сохраняется: подряд идущие дети группируются в один namespace-блок.
    // Узлы, опубликованные заголовком модуля, отсекаются в `visit_*` (объявление может лежать
    // и внутри блока интерфейса — верхнеуровневый фильтр его не покрыл бы).
    std::size_t i = 0;
    while (i < n.m_body.size()) {
        const bool outside = staysOutsideModuleNamespace(n.m_body[i]);
        std::size_t j = i;
        while (j < n.m_body.size() && staysOutsideModuleNamespace(n.m_body[j]) == outside) {
            ++j;
        }
        Sequence chunk(ParserToken::Kind::sequence);
        chunk.m_body.assign(n.m_body.begin() + static_cast<std::ptrdiff_t>(i), n.m_body.begin() + static_cast<std::ptrdiff_t>(j));
        if (outside) {
            m_driver.emitSequenceBody(chunk, out);
        } else {
            ModuleNamespaceGuard guard(m_ectx, out, moduleNamespace);
            m_driver.emitSequenceBody(chunk, out);
        }
        i = j;
    }
}

// -- Сайт импорта модуля: заголовок модуля вместо inline forward-decl --

void DeclEmitter::emitModuleImportDecls(const ModuleNode& n) {
    // Интерфейс модуля публикует его заголовок `<enc>.hpp`; сайт даёт `#include` для КАЖДОЙ части
    // (дедуп — в m_accumulators.requiredIncludes). Имя-зависимые факты (native-адрес, защита стека)
    // регистрируются из экспорт-набора КАЖДОЙ части: маски сайтов различаются.
    for (const auto& part : n.parts()) {
        EXPECT(part.projection && part.projection->exports && "module part must carry a site projection");
        registerImportedDeclFacts(*part.projection->exports);
        const std::string ns = m_ectx.moduleNamespace(part.index);
        EXPECT(!ns.empty() && "importable module must have an automatic C++ namespace");
        m_driver.m_type.recordRequiredInclude("#include \"" + ns + ".hpp\"");
    }
}

void DeclEmitter::registerImportedDeclFacts(const std::vector<AstNodePtr>& nodes) {
    // Экспорт-набор части — плоский список объявлений (namespace-путь уже в qualified-имени).
    for (const auto& node : nodes) {
        if (!node || !node->is<FuncDecl>()) {
            continue;
        }
        const FuncDecl& func = *node->as<FuncDecl>();
        if (func.m_isNativeImport || func.m_isNativeTemplateImport) {
            m_ectx.m_declFacts.registerNativeImport(&func, func.m_nativeName, func.text());
        }
        if (const auto guard = stackCheckGuardOf(func, m_ectx.m_ctx.attrs())) {
            m_ectx.m_declFacts.registerStackCheck(&func, *guard);
        }
    }
}

void DeclEmitter::emitModuleHeaderBody(const std::vector<AstNodePtr>& roots, const std::set<const AstNodeBase*>& terms, MapperFile out,
                                       std::string_view moduleNamespace) {
    // Корневой узел модуля (ModuleNode, НЕ сайт импорта) — прозрачный контейнер: модуль не
    // является namespace-сегментом. Объявления делятся на «внутри конверта» и «вне» тем же
    // правилом, что и в единице трансляции модуля (`staysOutsideModuleNamespace`): entry/`{% %}`
    // и нативные декларации (квалифицированное внешнее имя) внутри `namespace <enc>` недопустимы.
    // Два прохода: сначала ТИПЫ (прототипы функций/переменные ссылаются на их определения),
    // затем остальные объявления. Порядок внутри прохода — порядок тела (declare-before-use).
    for (const bool types_pass : {true, false}) {
        for (const auto& node : roots) {
            if (!node) {
                continue;
            }
            if (node->kind() == ParserToken::Kind::ModuleDecl && !static_cast<const ModuleNode&>(*node).isImport()) {
                emitModuleHeaderChunks(static_cast<const ModuleNode&>(*node).m_body, terms, out, moduleNamespace, types_pass);
                continue;
            }
            const std::vector<AstNodePtr> single{node};
            emitModuleHeaderChunks(single, terms, out, moduleNamespace, types_pass);
        }
    }
}

void DeclEmitter::emitModuleHeaderChunks(const std::vector<AstNodePtr>& body, const std::set<const AstNodeBase*>& terms, MapperFile out,
                                         std::string_view moduleNamespace, bool types_pass) {
    std::size_t i = 0;
    while (i < body.size()) {
        const bool outside = staysOutsideModuleNamespace(body[i]);
        std::size_t j = i;
        while (j < body.size() && staysOutsideModuleNamespace(body[j]) == outside) {
            ++j;
        }
        Sequence chunk(ParserToken::Kind::sequence);
        chunk.m_body.assign(body.begin() + static_cast<std::ptrdiff_t>(i), body.begin() + static_cast<std::ptrdiff_t>(j));
        if (outside || moduleNamespace.empty()) {
            emitImportScope(chunk.m_body, terms, out, types_pass);
        } else {
            ModuleNamespaceGuard guard(m_ectx, out, moduleNamespace);
            emitImportScope(chunk.m_body, terms, out, types_pass);
        }
        i = j;
    }
}

void DeclEmitter::emitImportScope(const std::vector<AstNodePtr>& body, const std::set<const AstNodeBase*>& terms, MapperFile out, bool types_pass) {
    for (const auto& node : body) {
        if (!node) {
            continue;
        }
        if (node->kind() == ParserToken::Kind::ScopeBlock) {
            const auto& sb = static_cast<const ScopeBlock&>(*node);
            // Блок интерфейса файла - прозрачный container: объявления эмитятся без обёртки.
            if (sb.isModuleInterface()) {
                emitImportScope(sb.m_body, terms, out, types_pass);
                continue;
            }
            // Анонимная область `_` и безымянный кодовый блок - не экспортируются.
            if (sb.is_private() || sb.is_anonymous()) {
                continue;
            }
            if (sb.is_protected()) {
                emitImportScope(sb.m_body, terms, out, types_pass);
                continue;
            }
            const std::string_view text = sb.text();
            // Глобальная область `::` - содержимое без обёртки.
            if (text == "::") {
                emitImportScope(sb.m_body, terms, out, types_pass);
                continue;
            }
            // Именованная область `ns::` - оборачиваем в `namespace ns { ... }`.
            const std::string nsName = utils::cppNamespacePath(m_ectx.m_output.namespaceCppName(text));
            m_ectx.m_output.namespaceStack.push_back(nsName);
            m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});
            m_ectx.m_ctx.source().output_append(out, m_ectx.m_output.indentPrefix() + "namespace " + nsName + " {\n");
            emitImportScope(sb.m_body, terms, out, types_pass);
            m_ectx.m_output.scopeStack.pop_back();
            m_ectx.m_ctx.source().output_append(out, m_ectx.m_output.indentPrefix() + "}\n");
            m_ectx.m_output.namespaceStack.pop_back();
            continue;
        }

        // Только отобранные экспорты. Кодоген работает на HIR-клоне: у клона origin указывает на
        // исходный AST-узел, по которому Pipeline отобрал публикуемые объявления.
        const AstNodeBase* exportKey = node->origin != nullptr ? node->origin : node.get();
        if (terms.find(exportKey) == terms.end()) {
            continue;
        }
        // Два прохода заголовка: сначала ТИПЫ (их определения нужны прототипам), затем остальное.
        const bool is_type = node->kind() == ParserToken::Kind::TypeDecl;
        if (is_type != types_pass) {
            continue;
        }
        m_ectx.m_ctx.source().output_append(out, m_ectx.m_output.indentPrefix());
        if (node->is<VarDecl>()) {
            generateVarDeclToFile(*node->as<VarDecl>(), out);
        } else if (node->is<FuncDecl>()) {
            const FuncDecl& fn = *node->as<FuncDecl>();
            if (bodyMustBeInModuleHeader(fn, m_ectx.m_ctx.attrs())) {
                if (bodyUsesNonPublishedEntity(fn, m_ectx.m_declFacts.headerPublishedDecls, m_ectx.m_output.currentModule)) {
                    m_ectx.m_ctx.report(fn.range(), diag::DiagId::ParseError,
                                        "function '{}' references a module-level entity that the module header does not publish "
                                        "(e.g. a '__::' helper): a template or constexpr function must be defined in the header; "
                                        "move the helper out of '__::' or drop the template",
                                        fn.text());
                    continue;
                }
                BodyEmitInHeaderGuard guard(m_ectx);
                generateFuncDeclToFile(fn, out);
            } else {
                generateFuncDeclToFile(fn, out);
            }
        } else if (is_type) {
            generateTypeDeclToFile(*node->as<Binary>(), out);
        }
        // прочие экспорт-формы пока не эмитятся
        m_ectx.m_ctx.source().output_append(out, "\n");
    }
}

} // namespace trust
