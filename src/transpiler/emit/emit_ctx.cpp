#include "transpiler/emit/emit_ctx.hpp"

#include "ast/declaration_ids.hpp"
#include "attrs/attr_builtin.hpp"
#include "diag/base_diags.hpp"

#include "diag/diag.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "types/type_id.hpp"
#include "target/target.hpp"

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace trust {

TypeCppFactsView CppEmitContext::cppFactsView() const {
    return TypeCppFactsView(m_services.readTypes(), m_services.target().typeCppFacts());
}

namespace {

/// `DeclId` объявления-носителя нативной C++-привязки (`nullptr`/не-объявление → INVALID):
/// привязка есть только у функций и переменных (как в `DeclCppFacts`); иначе — пусто.
[[nodiscard]] DeclId nativeBindingDeclIdOf(const AstNodeBase* node) noexcept {
    if (node != nullptr) {
        if (node->is<FuncDecl>()) {
            return node->as<FuncDecl>()->declId();
        }
        if (node->is<VarDecl>()) {
            return node->as<VarDecl>()->declId();
        }
    }
    return DeclId{};
}

} // namespace

std::string_view CppEmitContext::nativeCppNameOf(const AstNodeBase* decl) const noexcept {
    return m_services.declCppFacts().cppNameOf(nativeBindingDeclIdOf(decl));
}

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

CppEmitContext::CppEmitContext(CodegenServices services, analysis::BehavioralModes behavioral)
: m_services(std::move(services))
, m_behavioral(std::move(behavioral)) {
}

std::string CppEmitContext::cppName(std::string_view trustName, CppEntityKind kind) const {
    return trust::cppName(trustName, kind);
}

bool DeclFacts::isPublishedByHeader(const AstNodeBase* node) const {
    if (node == nullptr || publishedByHeader.empty()) {
        return false;
    }
    const DeclId id = declarationIdOf(*node);
    EXPECT(id.isValid() && "published-header check requires declaration identity (R23)");
    return publishedByHeader.count(id) != 0;
}

namespace {

/// Единое правило текстового поиска: индекс по тексту → `DeclId` → значение таблицы.
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

void DeclFacts::registerStackCheck(const AstNodeBase& decl, StackCheckGuard guard) {
    const DeclId id = declarationIdOf(decl);
    if (!id.isValid()) {
        // Синтетический узел (lowering/анализатор/тест) id не несёт — факт неадресуем и не хранится.
        // Для объявления РЕАЛЬНОГО модуля отсутствие id — дефект назначения идентичности (R23).
        EXPECT(!decl.module.isValid() && "module declaration must carry DeclId (R23)");
        return;
    }
    stackCheck[id] = guard;
}

void DeclFacts::registerFunctionName(const AstNodeBase& decl, std::string cppName, std::string_view trustText) {
    const DeclId id = declarationIdOf(decl);
    if (!id.isValid()) {
        EXPECT(!decl.module.isValid() && "module declaration must carry DeclId (R23)");
        return;
    }
    functionNames[id] = std::move(cppName);
    userNamesByText[std::string(trustText)].add(id);
}

const StackCheckGuard* DeclFacts::findStackCheck(DeclId decl) const noexcept {
    const auto it = stackCheck.find(decl);
    return it == stackCheck.end() ? nullptr : &it->second;
}

TextLookup DeclFacts::lookupFunctionCppNameByText(std::string_view text) const {
    return lookupByText(userNamesByText, functionNames, text);
}

std::string CppEmitContext::operatorCppName(std::string_view symbol, std::string_view trustNamespacePath) const {
    std::string cppOp = op::operatorCppName(symbol);
    EXPECT(!cppOp.empty() && "operatorCppName: unknown operator symbol");
    std::string out;
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
        // Имя функции — ДАННЫЕ ДЛЯ ЛИНКЕРА: при локальном резолве печатаем C++-имя объявления,
        // иначе — строку дословно (символ разрешит линкер). Резолв здесь — только ранняя диагностика
        // (не найдено/неоднозначно => warning), эмиссия от него не зависит.
        const TextLookup found = m_declFacts.lookupFunctionCppNameByText(nm);
        switch (found.status) {
        case TextLookup::Status::Found:
            out.push_back("reinterpret_cast<void*>(&" + *found.value + ")");
            break;
        case TextLookup::Status::Ambiguous:
            m_services.report(MapperRange(), diag::DiagId::StackCheckFnSymbol,
                              "ambiguous function '{}' in stack-check functions list; assuming an external linker symbol", nm);
            out.push_back("reinterpret_cast<void*>(&" + nm + ")");
            break;
        case TextLookup::Status::NotFound:
            m_services.report(MapperRange(), diag::DiagId::StackCheckFnSymbol,
                              "function '{}' in stack-check functions list is not declared in this translation unit; assuming an external linker symbol", nm);
            out.push_back("reinterpret_cast<void*>(&" + nm + ")");
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
    const auto* args = func.attr_args(*sc);
    if (args == nullptr || args->empty()) {
        return g; // `@[stack_check@]` без аргумента: size==0 → check_stack_limit()
    }
    // Семантика гарантирует РОВНО один аргумент из одних цифр (semantic/decl/func_signature_check.cpp),
    // но НЕ ограничивает величину: число, не помещающееся в long, НАСЫЩАЕТСЯ явно (check_overflow(N)
    // на реальном стеке всё равно бросает). «Магического» размера по умолчанию нет.
    unsigned long long value = 0;
    for (const char c : args->at(0)) {
        value = value * 10ULL + static_cast<unsigned long long>(c - '0');
        if (value > static_cast<unsigned long long>(std::numeric_limits<long>::max())) {
            value = static_cast<unsigned long long>(std::numeric_limits<long>::max());
            break;
        }
    }
    g.size = static_cast<long>(value);
    return g;
}

} // namespace trust
