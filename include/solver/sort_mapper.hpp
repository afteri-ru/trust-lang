#pragma once
// include/solver/sort_mapper.hpp
// Компонент маппинга trust-типов в SMT-сорта. Вынесен из монолитного trust_to_smt.cpp:
// не зависит от состояния TrustToSmt, работает только с реестром типов (Context).

#include "solver/smt_ast.hpp"
#include "types/type_id.hpp"

#include <optional>
#include <string_view>

namespace trust {
class Context;

namespace solver {

class SortMapper {
  public:
    explicit SortMapper(Context& ctx)
    : m_ctx(ctx) {}

    /// Имя типа → TypeId (реестр; без скоуп-стека). nullopt - не найдено.
    std::optional<TypeId> resolveTypeByName(std::string_view name) const;

    /// Тип → сорт. Единая точка маппинга (целые → BitVec, вещественные → Real, Bool → Bool,
    /// пользовательские → uninterpreted, структурные массивы → (Array (BitVec 64) elem)).
    std::optional<SmtSort> sortOf(TypeId id) const;

    /// Знаковый ли целочисленный тип (kIntegers → sign-extend; kUnsigned → zero-extend).
    bool isSignedType(TypeId id) const;

  private:
    Context& m_ctx;
};

} // namespace solver
} // namespace trust
