#pragma once

// include/ir/place_root.hpp
// Единая точка вычисления КОРНЕВОГО места выражения: Ident / поле / элемент / `&`/`*`.
// Используется адаптером CFG (projection пишется в корень) и flow-анализом представлений
// (источник/цель перепривязки). Возвращает узел объявления (resolvedDecl) либо nullptr.

#include "ast/ast_nodes.hpp"

namespace trust::flow {

[[nodiscard]] inline const AstNodeBase* placeRoot(const AstNodeBase* node) noexcept {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->kind() == ParserToken::Kind::Ident) {
        const auto* ident = node->as<IdentName>();
        return ident ? ident->resolvedDecl : nullptr;
    }
    if (node->kind() == ParserToken::Kind::ArrayAccess || node->kind() == ParserToken::Kind::MemberAccess) {
        const auto* binary = node->as<Binary>();
        return binary ? placeRoot(binary->m_left.get()) : nullptr;
    }
    if (node->is<RefMakeExpr>() || node->is<RefTakeExpr>()) {
        const auto* seq = node->as<Sequence>();
        return (seq && !seq->m_body.empty()) ? placeRoot(seq->m_body[0].get()) : nullptr;
    }
    return nullptr;
}

} // namespace trust::flow
