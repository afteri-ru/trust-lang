#pragma once

// Чтение элементов списков AST (ArgNode) — единый источник для анализаторов,
// без дублирования разбора формы позиции по месту.

#include "ast/ast_nodes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace trust {

// Имя и значение элемента коллекции из m_body: ArgNode → (text, m_value), иначе сам узел — значение.
inline void collectionElementNameValue(const AstNodeBase* el, std::string& name, const AstNodeBase*& value) {
    name.clear();
    value = el;
    if (!el) {
        return;
    }
    if (el->kind() == ParserToken::Kind::ArgNode) {
        const auto& a = static_cast<const ArgNode&>(*el);
        name = std::string(a.text());
        value = a.m_value.get();
    }
}

// Член enum/variant из ArgNode. Безнарный член `HIGH` (пустое имя, значение-Ident):
// имя лежит в значении, само значение отбрасывается.
struct EnumVariantMember {
    std::string name;
    AstNodePtr value; // nullptr — безнарный (нет значения)
    AstNodePtr type;  // явный тип (nullptr — нет)
};

inline EnumVariantMember enumVariantMember(const ArgNode& a) {
    EnumVariantMember m;
    m.name = std::string(a.text());
    m.value = a.m_value;
    m.type = a.m_type;
    if (m.name.empty() && m.value && m.value->kind() == ParserToken::Kind::Ident) {
        m.name = std::string(requiredText(*m.value));
        m.value = nullptr;
    }
    return m;
}

// Обход ArgNode-элементов списка объявления в порядке объявления; позиция нужна правилам trailing-pack.
template <typename Fn>
void forEachArgParam(const std::optional<std::vector<AstNodePtr>>& params, Fn&& fn) {
    if (!params.has_value()) {
        return;
    }
    for (std::size_t i = 0; i < params->size(); ++i) {
        const auto& node = (*params)[i];
        if (!node || node->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        fn(static_cast<const ArgNode&>(*node), i);
    }
}

} // namespace trust
