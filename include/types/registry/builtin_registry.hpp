#pragma once

// Зона «встроенные определения»: доступ к общему иммутабельному ядру BuiltinTypeCore и языковой
// предикат абстрактного структурного контейнера. Целевые C++-факты (реализации/привязки) живут ВНЕ
// языковой модели — в компоненте `target` (R37); здесь их нет.

#include "types/registry/builtin_type_core.hpp"
#include "types/type_id.hpp"

#include <string_view>

namespace trust {

class TypeStore;

class BuiltinRegistry {
  public:
    explicit BuiltinRegistry(TypeStore& store) noexcept
    : m_store(store) {}

    /// Абстрактный builtin-тип, которому принадлежит реализация структурного контейнера
    /// (`Array<T>` → `:Array`, `Range<T>` → `:Range`, `Tensor<T,shape>` → `:Tensor`/`:SparseTensor`).
    /// INVALID — id не является структурным контейнером. ЕДИНАЯ точка «где живёт реализация инстанса»:
    /// используется provider-gate и аксессорами целевых фактов (не дублировать).
    [[nodiscard]] TypeId abstractBuiltinOf(TypeId id) const noexcept;

  private:
    TypeStore& m_store;
};

} // namespace trust
