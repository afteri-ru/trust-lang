#include "transpiler/emit/emit_ctx.hpp"

#include "attrs/attr_builtin.hpp"
#include "diag/base_diags.hpp"
#include "session/context.hpp"

#include "diag/diag.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "types/type_id.hpp"

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace trust {

ResultGuard::ResultGuard(CppEmitContext& ectx, std::string name, std::string cpp)
: m_ectx(ectx)
, m_savedName(std::move(ectx.m_result.name))
, m_savedCpp(std::move(ectx.m_result.cpp)) {
    ectx.m_result.name = std::move(name);
    ectx.m_result.cpp = std::move(cpp);
}

ResultGuard::~ResultGuard() {
    m_ectx.m_result.name = std::move(m_savedName);
    m_ectx.m_result.cpp = std::move(m_savedCpp);
}

ActiveTemplateParamsGuard::ActiveTemplateParamsGuard(CppEmitContext& ectx, std::vector<std::string> names)
: m_ectx(ectx)
, m_saved(std::move(ectx.m_activeTemplateParams)) {
    m_ectx.m_activeTemplateParams = std::move(names);
}

ActiveTemplateParamsGuard::~ActiveTemplateParamsGuard() {
    m_ectx.m_activeTemplateParams = std::move(m_saved);
}

CppEmitContext::CppEmitContext(Context& ctx, analysis::BehavioralModes behavioral)
: m_ctx(ctx)
, m_behavioral(std::move(behavioral)) {
}

std::string CppEmitContext::moduleNamespace(ModuleIndex owner) const {
    if (!owner.isValid()) {
        return {};
    }
    // Провайдер имени модуля — у реестра типов (единый источник для типов и кодогена;
    // FAULT без него, чтобы изоляция не потерялась молча).
    return m_ctx.readTypes().printer().moduleCppNamespace(owner);
}

std::string CppEmitContext::cppName(std::string_view trustName, CppEntityKind kind, ModuleIndex owner) const {
    return trust::cppName(trustName, kind, moduleNamespace(owner));
}

std::string CppEmitContext::cppFuncName(std::string_view trustName, bool isOperator, bool isEntry, ModuleIndex owner) const {
    return trust::cppFuncName(trustName, isOperator, isEntry, moduleNamespace(owner));
}

ModuleNamespaceGuard::ModuleNamespaceGuard(CppEmitContext& ectx, MapperFile out, std::string_view ns) noexcept
: m_ectx(ectx)
, m_out(out) {
    if (!ns.empty()) {
        ectx.m_ctx.source().output_append(out, "namespace " + std::string(ns) + " {\n");
        m_open = true;
    }
}

ModuleNamespaceGuard::~ModuleNamespaceGuard() {
    if (m_open) {
        // Форвард-decl заканчивается ';' без перевода строки — закрываем с новой строки.
        const std::string_view body = m_ectx.m_ctx.source().output_body(m_out);
        if (!body.empty() && body.back() != '\n') {
            m_ectx.m_ctx.source().output_append(m_out, "\n");
        }
        m_ectx.m_ctx.source().output_append(m_out, "}\n");
    }
}

bool DeclFacts::isPublishedByHeader(const AstNodeBase* node) const {
    if (node == nullptr || publishedByHeader.empty()) {
        return false;
    }
    const AstNodeBase* key = node->origin != nullptr ? node->origin : node;
    return publishedByHeader.count(key) != 0;
}

namespace {

/// Единое правило текстового поиска: индекс по тексту → идентичность → значение таблицы.
/// Отсутствие и неоднозначность различаются (диагностику формирует вызывающий: у него свой
/// текст сообщения и своя позиция).
template <typename Table>
TextLookup lookupByText(const std::unordered_map<std::string, TextDeclarationIndex>& index, const Table& table, std::string_view text) {
    TextLookup result;
    const auto entry = index.find(std::string(text));
    if (entry == index.end()) {
        return result;
    }
    if (entry->second.ambiguous) {
        result.status = TextLookup::Status::Ambiguous;
        return result;
    }
    const auto value = table.find(entry->second.decl);
    EXPECT(value != table.end() && "text index and declaration table are written together");
    result.status = TextLookup::Status::Found;
    result.value = &value->second;
    return result;
}

} // namespace

void DeclFacts::registerNativeImport(const AstNodeBase* decl, std::string nativeName, std::string_view trustText) {
    const AstNodeBase* identity = declIdentity(decl);
    nativeImports[identity] = std::move(nativeName);
    nativeImportsByText[std::string(trustText)].add(identity);
}

void DeclFacts::registerStackCheck(const AstNodeBase* decl, StackCheckGuard guard) {
    stackCheck[declIdentity(decl)] = guard;
}

void DeclFacts::registerFunctionName(const AstNodeBase* decl, std::string cppName, std::string_view trustText) {
    const AstNodeBase* identity = declIdentity(decl);
    functionNames[identity] = std::move(cppName);
    functionNamesByText[std::string(trustText)].add(identity);
}

const std::string* DeclFacts::findNativeImport(const AstNodeBase* decl) const noexcept {
    const auto it = nativeImports.find(declIdentity(decl));
    return it == nativeImports.end() ? nullptr : &it->second;
}

const StackCheckGuard* DeclFacts::findStackCheck(const AstNodeBase* decl) const noexcept {
    const auto it = stackCheck.find(declIdentity(decl));
    return it == stackCheck.end() ? nullptr : &it->second;
}

TextLookup DeclFacts::lookupNativeImportByText(std::string_view text) const {
    return lookupByText(nativeImportsByText, nativeImports, text);
}

TextLookup DeclFacts::lookupFunctionCppNameByText(std::string_view text) const {
    return lookupByText(functionNamesByText, functionNames, text);
}

std::string CppEmitContext::operatorCppName(std::string_view symbol, std::string_view trustNamespacePath, ModuleIndex owner) const {
    std::string cppOp = op::operatorCppName(symbol);
    EXPECT(!cppOp.empty() && "operatorCppName: unknown operator symbol");
    // Модуль — ПЕРВЫЙ сегмент; неимпортируемый юнит (root без заголовка) не изолируется.
    const std::string moduleNamespace = owner.isValid() ? m_ctx.readTypes().printer().moduleCppNamespace(owner) : std::string{};
    std::string out;
    if (!moduleNamespace.empty()) {
        out += moduleNamespace;
        out += "::";
    }
    if (!trustNamespacePath.empty()) {
        out += utils::cppNamespacePath(trustNamespacePath);
        out += "::";
    }
    out += cppOp;
    return out;
}

std::string OutputState::qualifiedCppName(std::string_view name) const {
    if (namespaceStack.empty()) {
        return std::string(name);
    }
    std::string q;
    for (const auto& ns : namespaceStack) {
        q += ns;
        q += "::";
    }
    q += name;
    return q;
}

std::string OutputState::namespaceCppName(std::string_view text) {
    std::string_view s = text;
    if (s.rfind("::", 0) == 0) {
        s.remove_prefix(2);
    }
    if (s.size() >= 2 && s.substr(s.size() - 2) == "::") {
        s.remove_suffix(2);
    }
    return std::string(s);
}

std::vector<std::string> CppEmitContext::resolveStackCheckAddresses(const std::vector<std::string>& names) {
    std::vector<std::string> out;
    for (const auto& nm : names) {
        // Текстовый домен (`--stack-check-functions`): одноимённые функции разных модулей/
        // namespace не разрешаются «первым победителем» — это ошибка конфигурации.
        const TextLookup found = m_declFacts.lookupFunctionCppNameByText(nm);
        switch (found.status) {
        case TextLookup::Status::Found:
            out.push_back("reinterpret_cast<void*>(&" + *found.value + ")");
            break;
        case TextLookup::Status::Ambiguous:
            m_ctx.report(MapperRange(), diag::DiagId::ParseError,
                         "ambiguous function '{}' in stack-check functions list: it names same-named declarations of different modules or namespaces", nm);
            break;
        case TextLookup::Status::NotFound:
            m_ctx.report(MapperRange(), diag::DiagId::ParseError, "unknown function '{}' in stack-check functions list", nm);
            break;
        }
    }
    return out;
}

std::optional<StackCheckGuard> stackCheckGuardOf(const FuncDecl& func, const AttrPool& attrs) {
    const auto sc = attrs.lookup(attr::StackCheck);
    if (!sc.has_value() || !func.has_attr(*sc)) {
        return std::nullopt;
    }
    StackCheckGuard g;
    if (const auto* args = func.attr_args(*sc); args && !args->empty()) {
        try {
            const long v = std::stol(args->at(0));
            g.size = (v < 0) ? 0 : v;
        } catch (const std::exception&) {
            g.size = 1000000000L; // ~1 ГБ свободного стека — на реальном стеке всегда бросок
        }
    }
    return g;
}

} // namespace trust
