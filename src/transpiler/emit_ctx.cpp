#include "transpiler/emit_ctx.hpp"

#include "types/type_id.hpp"

#include <string>
#include <utility>

namespace trust {

ResultGuard::ResultGuard(CppEmitContext& ectx, std::string name, std::string cpp)
: m_ectx(ectx)
, m_savedName(std::move(ectx.m_resultName))
, m_savedCpp(std::move(ectx.m_resultCpp)) {
    ectx.m_resultName = std::move(name);
    ectx.m_resultCpp = std::move(cpp);
}

ResultGuard::~ResultGuard() {
    m_ectx.m_resultName = std::move(m_savedName);
    m_ectx.m_resultCpp = std::move(m_savedCpp);
}

CppEmitContext::CppEmitContext(Context& ctx, const SymbolTable* resolvedTypes)
: m_ctx(ctx)
, m_resolvedTypes(resolvedTypes) {
}

std::string CppEmitContext::qualifiedCppName(std::string_view name) const {
    if (m_namespaceStack.empty()) {
        return std::string(name);
    }
    std::string q;
    for (const auto& ns : m_namespaceStack) {
        q += ns;
        q += "::";
    }
    q += name;
    return q;
}

std::string CppEmitContext::namespaceCppName(std::string_view text) {
    std::string_view s = text;
    if (s.rfind("::", 0) == 0) {
        s.remove_prefix(2);
    }
    if (s.size() >= 2 && s.substr(s.size() - 2) == "::") {
        s.remove_suffix(2);
    }
    return std::string(s);
}

} // namespace trust
