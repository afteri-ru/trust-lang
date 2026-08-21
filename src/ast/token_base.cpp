// token_base.cpp - AstNodeBase and AstNodeAttr implementation for methods that require AttrPool.
//
// These methods are separated here to avoid circular dependencies.
// Dump methods for specialized nodes live in ast_nodes.cpp and ident_name.cpp.

#include "ast/token_base.hpp"
#include "ast/attr_pool.hpp"
#include "syntax/term.h"
#include <algorithm>

namespace trust {

// ----------------------------------------------------------------------------
// AstNodeBase::text / range - читаются из исходного Term (m_term).
// Узел без m_term при обращении к text()/range() - ошибка логики.
// ----------------------------------------------------------------------------

std::string_view AstNodeBase::text() const {
    EXPECT(m_term && "text() requires a source Term");
    return m_term->getText();
}

MapperRange AstNodeBase::range() const {
    // Узел без m_term (ручной/test-only, синтетический) не имеет исходного диапазона:
    // возвращаем invalid range вместо EXPECT. Это необходимо, т.к. диапазоны родителей
    // вычисляются на лету по узлам-детям (spanOfNodes) - ручные дети обязаны «мягко»
    // сообщать об отсутствии source-range, а не бросать.
    return m_term ? m_term->m_mapperRange : MapperRange{};
}

// ----------------------------------------------------------------------------
// AstNodeBase::dump - default dump for base token
// ----------------------------------------------------------------------------

std::string AstNodeBase::dump(size_t indent) const {
    std::string result(indent, ' ');
    result += ParserToken::name(kind());
    return result;
}

// ----------------------------------------------------------------------------
// AstNodeAttr::has_attr (by name)
// ----------------------------------------------------------------------------

bool AstNodeAttr::has_attr(const AttrPool& pool, std::string_view name) const {
    auto id = pool.lookup(name);
    if (!id.has_value()) {
        return false;
    }
    return has_attr(id.value());
}

// ----------------------------------------------------------------------------
// AstNodeAttr::set_attr_args / attr_args - аргументы атрибута (напр. @[link("m")] → ["m"]).
// Хранятся по индексу атрибута в пуле (id & kAttrIndexMask).
// ----------------------------------------------------------------------------

void AstNodeAttr::set_attr_args(AttrId id, std::vector<std::string> args) {
    const AttrId key = id & detail::kAttrIndexMask;
    m_attrArgs[key] = std::move(args);
}

const std::vector<std::string>* AstNodeAttr::attr_args(AttrId id) const noexcept {
    const AttrId key = id & detail::kAttrIndexMask;
    auto it = m_attrArgs.find(key);
    if (it == m_attrArgs.end()) {
        return nullptr;
    }
    return &it->second;
}

// ----------------------------------------------------------------------------
// AstNodeAttr::dump - default dump for attribute-aware token
// ----------------------------------------------------------------------------

std::string AstNodeAttr::dump(size_t indent) const {
    return AstNodeBase::dump(indent);
}

} // namespace trust