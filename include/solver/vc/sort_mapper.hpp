#pragma once
// include/solver/vc/sort_mapper.hpp
// Компонент маппинга trust-типов в SMT-сорта. Вынесен из монолитного trust_to_smt.cpp:
// не зависит от состояния TrustToSmt, работает только с реестром типов (Context).

#include "solver/smt_ast.hpp"
#include "ast/ident_name.hpp"
#include "types/type_id.hpp"

#include <optional>
#include <string_view>

namespace trust {
class Context;
class AstNodeBase;

namespace solver {

/// Тип, МАТЕРИАЛИЗОВАННЫЙ семантикой в узле-имени типа (`IdentName::resolvedTypeId`).
/// `fallback` — тип из другого факта узла (например, `ArgNode::resultType`), если имя не
/// разрешено. Имена резолвит ТОЛЬКО семантика: повторный поиск по тексту в бэкендах запрещён.
[[nodiscard]] inline TypeId resolvedTypeOf(const AstNodeBase* typeNode, TypeId fallback = INVALID_TYPE_ID) {
    if (typeNode != nullptr && typeNode->is<IdentName>()) {
        const TypeId resolved = typeNode->as<IdentName>()->resolvedTypeId;
        if (resolved != INVALID_TYPE_ID) {
            return resolved;
        }
    }
    return fallback;
}

class SortMapper {
  public:
    explicit SortMapper(Context& ctx)
    : m_ctx(ctx) {}

    /// Тип → сорт. Единая точка маппинга (целые → BitVec, вещественные → Real, Bool → Bool,
    /// пользовательские → uninterpreted, структурные массивы → (Array (BitVec 64) elem)).
    std::optional<SmtSort> sortOf(TypeId id) const;

    /// Знаковый ли целочисленный тип (kIntegers → sign-extend; kUnsigned → zero-extend).
    bool isSignedType(TypeId id) const;

    /// Знак ЦЕЛОЧИСЛЕННОГО типа: true — знаковый (kIntegers), false — беззнаковый (kUnsigned),
    /// nullopt — тип не целочисленный (Bool/Real/функциональный/пользовательский/INVALID).
    /// Нужен там, где факт типа может оказаться не-целым (напр. имя функции в пост-условии):
    /// такой тип не является источником знака, кандидат пропускается.
    std::optional<bool> integerSignOf(TypeId id) const;

  private:
    Context& m_ctx;
};

} // namespace solver
} // namespace trust
