// attr_builtin.cpp — register_builtin_attrs implementation and Attr::to_string

#include "ast/attr_builtin.hpp"
#include <string>

namespace trust {

void register_builtin_attrs(AttrPool& pool) {
    using namespace attr_names;

    // Attributes without required parameters
    pool.register_attr(kConst, std::vector<AttrParamType>{}, BuiltinAttrKind::kConst);
    pool.register_attr(kPure, std::vector<AttrParamType>{}, BuiltinAttrKind::kPure);
    pool.register_attr(kSend, std::vector<AttrParamType>{}, BuiltinAttrKind::kSend);
    pool.register_attr(kSync, std::vector<AttrParamType>{}, BuiltinAttrKind::kSync);
    pool.register_attr(kThread, std::vector<AttrParamType>{}, BuiltinAttrKind::kThread);
    pool.register_attr(kReadOnly, std::vector<AttrParamType>{}, BuiltinAttrKind::kReadOnly);
    pool.register_attr(kNoExcept, std::vector<AttrParamType>{}, BuiltinAttrKind::kNoExcept);
    pool.register_attr(kStackGuard, std::vector<AttrParamType>{}, BuiltinAttrKind::kStackGuard);

    // @trust has one required string parameter (the trusted assertion)
    pool.register_attr(kTrust, std::vector<AttrParamType>{AttrParamType::kString}, BuiltinAttrKind::kTrust);

    // @require / @ensure take a range parameter (code block or expression)
    pool.register_attr(kRequire, std::vector<AttrParamType>{AttrParamType::kRange}, BuiltinAttrKind::kRequire);
    pool.register_attr(kEnsure, std::vector<AttrParamType>{AttrParamType::kRange}, BuiltinAttrKind::kEnsure);
}

// ────────────────────────────────────────────────────────────────────────────
// Attr::to_string — human-readable representation
// ────────────────────────────────────────────────────────────────────────────

std::string Attr::to_string() const {
    std::string result(m_name);

    if (m_params.has_value() && !m_params->empty()) {
        result += "(";
        for (std::size_t i = 0; i < m_params->size(); ++i) {
            if (i > 0)
                result += ", ";
            const auto& p = (*m_params)[i];
            if (p.is_int()) {
                result += std::to_string(p.as_int());
            } else if (p.is_string()) {
                result += "\"";
                result += p.as_string();
                result += "\"";
            } else if (p.is_range()) {
                result += "[";
                result += std::to_string(p.as_range().begin.offset());
                result += "..";
                result += std::to_string(p.as_range().end.offset());
                result += "]";
            }
        }
        result += ")";
    }

    return result;
}

} // namespace trust
