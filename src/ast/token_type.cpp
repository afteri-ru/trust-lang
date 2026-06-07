// token_type.cpp — реализация IdentType::dump()

#include "ast/token_type.hpp"

namespace trust {

// ────────────────────────────────────────────────────────────────────────────
// IdentType::dump
// ────────────────────────────────────────────────────────────────────────────

std::string IdentType::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    if (!text().empty()) {
        result += " '";
        result += ":";
        result += text();
        result += "'";
    }

    if (m_dims.has_value() && !m_dims->empty()) {
        result += " [";
        for (size_t i = 0; i < m_dims->size(); ++i) {
            if (i > 0)
                result += ", ";
            const auto& node = (*m_dims)[i];
            if (node)
                result += node->dump(indent + 2);
        }
        result += "]";
    }

    if (m_params.has_value() && !m_params->empty()) {
        result += " (";
        for (size_t i = 0; i < m_params->size(); ++i) {
            const auto& node = (*m_params)[i];
            if (node)
                result += node->dump(indent + 2);
        }
        result += ")";
    }

    return result;
}

} // namespace trust