#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/emit/decl_emit_common.hpp"
#include "analysis/builtin_class.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/declaration_ids.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "location/module_name.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "target/runtime_symbols.hpp"
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
            // Синтезируемая entry — глобальный символ (вне областей имён). Изоляция корня здесь не нужна:
            // импорт корня образовал бы цикл, поэтому на его символы никто снаружи не ссылается.
            m_driver.emitSingleFileModuleBody(n, m_ectx.m_output.out);
            m_ectx.m_output.currentModule = previousModule;
            return;
        }
    }
    emitModuleBodyIsolated(n, m_ectx.m_output.out);
    m_ectx.m_output.currentModule = previousModule;
}

void DeclEmitter::emitModuleBodyIsolated(const ModuleNode& n, MapperFile out) {
    // Модуль не создаёт автоматического C++ namespace: символы модуля эмитятся в их trust-namespace
    // (изоляция одноимённых экспортов разных модулей — ответственность программиста через `ns::`).
    m_driver.emitSequenceBody(n, out);
}

void DeclEmitter::emitModuleImportDecls(const ModuleNode& n) {
    // Интерфейс модуля публикует его заголовок `<enc>.hpp`; сайт даёт `#include` для КАЖДОЙ части
    // (дедуп — в m_accumulators.requiredIncludes). Имя-зависимые факты (native-адрес, защита стека)
    // регистрируются из экспорт-набора КАЖДОЙ части: маски сайтов различаются.
    for (const auto& part : n.parts()) {
        EXPECT(part.projection && part.projection->exports && "module part must carry a site projection");
        registerImportedDeclFacts(*part.projection->exports);
        // Имя заголовка модуля выводится из логического имени (структурная согласованность
        // артефактов сборки); символы ABI модуль в себе не содержит.
        const ModuleResolver* resolver = m_ectx.m_services.moduleResolver();
        EXPECT(resolver != nullptr && "module_emit: module resolver is not set (Pipeline must inject it)");
        const std::string headerName = moduleEncodedName(resolver->moduleLogicalName(part.index));
        EXPECT(!headerName.empty() && "importable module must have an encoded header name");
        m_driver.m_type.recordRequiredInclude("#include \"" + moduleHeaderFileName(headerName) + "\"");
    }
}

void DeclEmitter::registerImportedDeclFacts(const std::vector<AstNodePtr>& nodes) {
    // Экспорт-набор части — плоский список объявлений (namespace-путь уже в qualified-имени).
    for (const auto& node : nodes) {
        if (!node || !node->is<FuncDecl>()) {
            continue;
        }
        const FuncDecl& func = *node->as<FuncDecl>();
        if (const auto guard = stackCheckGuardOf(func, m_ectx.m_services.attrs())) {
            m_ectx.m_declFacts.registerStackCheck(func, *guard);
        }
    }
}

void DeclEmitter::emitModuleHeaderBody(const std::vector<AstNodePtr>& roots, const std::set<DeclId>& terms, MapperFile out) {
    // Корневой узел модуля (ModuleNode, НЕ сайт импорта) — прозрачный контейнер: модуль не
    // является namespace-сегментом. Модуль не создаёт автоматического C++ namespace; объявления
    // эмитятся в их trust-namespace.
    // Три прохода: forward-декларации классов (сигнатуры членов могут ссылаться на тип,
    // определённый ниже в том же заголовке), затем определения ТИПОВ (прототипы функций/переменные
    // ссылаются на них), затем остальные объявления. Порядок внутри прохода — порядок тела.
    for (const HeaderPass pass : {HeaderPass::kForwardDecls, HeaderPass::kTypes, HeaderPass::kRest}) {
        for (const auto& node : roots) {
            if (!node) {
                continue;
            }
            if (node->kind() == ParserToken::Kind::ModuleDecl && !static_cast<const ModuleNode&>(*node).isImport()) {
                emitImportScope(static_cast<const ModuleNode&>(*node).m_body, terms, out, pass);
                continue;
            }
            const std::vector<AstNodePtr> single{node};
            emitImportScope(single, terms, out, pass);
        }
    }
}

void DeclEmitter::emitClassForwardDecl(const AstNodeBase& node, MapperFile out) {
    if (node.kind() != ParserToken::Kind::TypeDecl || !node.is<Binary>()) {
        return;
    }
    const auto& typeDecl = *node.as<Binary>();
    const AstNodeBase* right = typeDecl.m_right.get();
    if (right == nullptr) {
        return;
    }
    TypeId typeId = INVALID_TYPE_ID;
    if (typeDecl.m_left && typeDecl.m_left->is<IdentName>()) {
        typeId = typeDecl.m_left->as<IdentName>()->resolvedTypeId;
    }
    if (right->is<RecordDecl>()) {
        return; // записи объявляются/определяются по месту (declare-before-use в .src)
    }
    if (!right->is<ClassDecl>()) {
        return;
    }
    const auto& cls = *right->as<ClassDecl>();
    const std::string_view native = m_ectx.m_services.declCppFacts().cppNameOf(cls.declId());
    if (native.empty()) {
        return;
    }
    const bool builtin = typeId != INVALID_TYPE_ID && m_ectx.m_services.readTypes().store().isBuiltinType(typeId);
    if (builtin) {
        if (!analysis::builtinImplGeneratesCppClass(cls, m_ectx.m_services.attrs())) {
            return; // C++-тип приходит из внешнего заголовка — forward не нужен
        }
        // native C++-имя (`trust::StrChar`) задаёт namespace forward-декларации.
        const CppQualifiedParts parts = splitCppName(native);
        if (parts.name.empty()) {
            return;
        }
        std::string text;
        for (const std::string& ns : parts.namespaces) {
            text += "namespace " + ns + " { ";
        }
        text += "class " + parts.name + ";";
        for (std::size_t i = parts.namespaces.size(); i > 0; --i) {
            text += " }";
        }
        m_ectx.m_services.source().output_append(out, text + "\n");
        return;
    }
    // Обёртка нативного типа (`::=`): её интерфейс — внешний C++-заголовок, forward не нужен.
}

void DeclEmitter::emitImportScope(const std::vector<AstNodePtr>& body, const std::set<DeclId>& terms, MapperFile out, HeaderPass pass) {
    for (const auto& node : body) {
        if (!node) {
            continue;
        }
        if (node->kind() == ParserToken::Kind::ScopeBlock) {
            const auto& sb = static_cast<const ScopeBlock&>(*node);
            // Блок интерфейса файла - прозрачный container: объявления эмитятся без обёртки.
            if (sb.isModuleInterface()) {
                emitImportScope(sb.m_body, terms, out, pass);
                continue;
            }
            // Анонимная область `_` и безымянный кодовый блок - не экспортируются.
            if (sb.is_private() || sb.is_anonymous()) {
                continue;
            }
            if (sb.is_protected()) {
                emitImportScope(sb.m_body, terms, out, pass);
                continue;
            }
            const std::string_view text = sb.text();
            // Глобальная область `::` - содержимое без обёртки.
            if (text == "::") {
                emitImportScope(sb.m_body, terms, out, pass);
                continue;
            }
            // Именованная область `ns::` - оборачиваем в `namespace ns { ... }`.
            const std::string nsName = utils::cppNamespacePath(m_ectx.m_output.namespaceCppName(text));
            m_ectx.m_output.namespaceStack.push_back(nsName);
            m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel() + 1});
            m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix() + "namespace " + nsName + " {\n");
            emitImportScope(sb.m_body, terms, out, pass);
            m_ectx.m_output.scopeStack.pop_back();
            m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix() + "}\n");
            m_ectx.m_output.namespaceStack.pop_back();
            continue;
        }

        // Только отобранные экспорты. Идентичность узла — `DeclId` (R23): HIR-клон сохраняет id исходного
        // объявления, поэтому повторного сопоставления по адресу (`origin`) не требуется.
        const DeclId exportKey = declarationIdOf(*node);
        if (!exportKey.isValid() || !terms.contains(exportKey)) {
            continue;
        }
        const bool is_type = node->kind() == ParserToken::Kind::TypeDecl;
        // Проходы: forward-декларации классов -> определения типов -> остальные объявления.
        if (pass != HeaderPass::kForwardDecls && (is_type != (pass == HeaderPass::kTypes))) {
            continue;
        }
        if (pass == HeaderPass::kForwardDecls) {
            emitClassForwardDecl(*node, out);
            continue;
        }
        m_ectx.m_services.source().output_append(out, m_ectx.m_output.indentPrefix());
        if (node->is<VarDecl>()) {
            generateVarDeclToFile(*node->as<VarDecl>(), out);
        } else if (node->is<FuncDecl>()) {
            const FuncDecl& fn = *node->as<FuncDecl>();
            if (bodyMustBeInModuleHeader(fn, m_ectx.m_services.attrs())) {
                if (headerBodyUnpublishable(m_ectx, fn)) {
                    continue;
                }
                BodyEmitInHeaderGuard guard(m_ectx);
                generateFuncDeclToFile(fn, out);
            } else {
                generateFuncDeclToFile(fn, out);
            }
        } else if (is_type) {
            generateTypeDeclToFile(*node->as<Binary>(), out);
        } else {
            // Экспорт-набор заголовка содержит только VarDecl/FuncDecl/TypeDecl (отбор — в
            // pipeline/backend/emit.cpp). Иной kind здесь — нарушение внутреннего контракта,
            // а не «пока не эмитим»: тихий пропуск объявления молча ломал бы заголовок.
            faultUnexpectedKind("DeclEmitter(module header)", *node);
        }
        m_ectx.m_services.source().output_append(out, "\n");
    }
}

} // namespace trust
