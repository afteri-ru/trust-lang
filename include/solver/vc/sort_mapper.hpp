#pragma once
// Компонент маппинга trust-типов в SMT-сорта. Вынесен из монолитного trust_to_smt.cpp:
// не зависит от состояния TrustToSmt, работает только с реестром типов (через read-model порт).

#include "solver/smt_ast.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "types/type_id.hpp"

#include <optional>
#include <string_view>

namespace trust {
struct RunServices;
class AstNodeBase;

namespace solver {

/// Тип, МАТЕРИАЛИЗОВАННЫЙ семантикой в узле-имени типа: `IdentName::resolvedTypeId` либо
/// `IdentType::resolvedState` (полный тип аннотации). `fallback` — тип из другого факта узла
/// (например, `ArgNode::resultType`); вызывающий обязан передать его явно — неявного «нет типа»
/// по умолчанию нет. Имена резолвит ТОЛЬКО семантика: повторный поиск по тексту в бэкендах запрещён.
[[nodiscard]] inline TypeId resolvedTypeOf(const AstNodeBase* typeNode, TypeId fallback) {
    if (typeNode == nullptr) {
        return fallback;
    }
    if (typeNode->is<IdentType>()) {
        const QualType state = typeNode->as<IdentType>()->resolvedState;
        if (state.isValid()) {
            return state.getTypeId();
        }
    }
    if (typeNode->is<IdentName>()) {
        const TypeId resolved = typeNode->as<IdentName>()->resolvedTypeId;
        if (resolved != INVALID_TYPE_ID) {
            return resolved;
        }
    }
    return fallback;
}

class SortMapper {
  public:
    explicit SortMapper(const RunServices& services)
    : m_services(services) {}

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
    const RunServices& m_services;
};

} // namespace solver
} // namespace trust
