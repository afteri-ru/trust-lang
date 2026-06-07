// token_base.cpp — AstNodeBase and AstNodeAttr implementation for methods that require AttrPool.
//
// These methods are separated here to avoid circular dependencies.
// Dump methods for specialized nodes live in ast_nodes.cpp and ident_name.cpp.

#include "ast/token_base.hpp"
#include "ast/attr_pool.hpp"
#include "syntax/term.h"
#include <algorithm>

namespace trust {

// ────────────────────────────────────────────────────────────────────────────
// AstNodeBase::text / range — читаются из исходного Term (m_term).
// Узел без m_term при обращении к text()/range() — ошибка логики.
// ────────────────────────────────────────────────────────────────────────────

std::string_view AstNodeBase::text() const {
    EXPECT(m_term && "text() requires a source Term");
    return m_term->getText();
}

MapperRange AstNodeBase::range() const {
    EXPECT(m_term && "range() requires a source Term");
    return m_term->m_mapperRange;
}

// ────────────────────────────────────────────────────────────────────────────
// AstNodeBase::dump — default dump for base token
// ────────────────────────────────────────────────────────────────────────────

std::string AstNodeBase::dump(size_t indent) const {
    std::string result(indent, ' ');
    result += ParserToken::name(kind());
    return result;
}

// ────────────────────────────────────────────────────────────────────────────
// AstNodeAttr::has_attr (by name)
// ────────────────────────────────────────────────────────────────────────────

bool AstNodeAttr::has_attr(const AttrPool& pool, std::string_view name) const {
    auto id = pool.lookup(name);
    if (!id.has_value())
        return false;
    return has_attr(id.value());
}

// ────────────────────────────────────────────────────────────────────────────
// AstNodeAttr::dump — default dump for attribute-aware token
// ────────────────────────────────────────────────────────────────────────────

std::string AstNodeAttr::dump(size_t indent) const {
    return AstNodeBase::dump(indent);
}

} // namespace trust