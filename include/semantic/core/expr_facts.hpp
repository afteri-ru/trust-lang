#pragma once

// ЕДИНСТВЕННЫЙ читатель/писатель type-state выражения в полях узлов AST.
// Чистые функции от AST + реестра типов: не зависят от завершённости обхода, поэтому
// одинаковы для ядра, хуков, верификатора и будущих бэкендов.

#include "ast/ast_nodes.hpp"
#include "types/type_id.hpp"
#include "types/typekind.hpp"

namespace trust {

class Context;
class TypeResolution;

class ExprFacts {
  public:
    ExprFacts(Context& ctx, const TypeResolution& types);

    /// Разрешённый тип узла (IDENTITY-only: qualifier-биты сняты у ВСЕХ узлов).
    [[nodiscard]] TypeId exprType(const AstNodeBase& node) const;
    /// Полный type-state (identity + const/inferred) из поля узла.
    [[nodiscard]] QualType exprState(const AstNodeBase& node) const;
    /// Сохраняет IDENTITY-тип результата выражения (state-биты запрещены - EXPECT).
    void setExprType(AstNodeBase* node, TypeId id);
    /// Сохраняет полный type-state результата выражения.
    void setExprState(AstNodeBase* node, QualType state);

  private:
    Context& m_ctx;
    const TypeResolution& m_types;
};

} // namespace trust
