// Целевые привязки нативных методов: метод интерфейсного builtin-типа → литеральное C++-имя
// реализации (делегат). ЕДИНСТВЕННЫЙ владелец (R1); принадлежит контракту цели (`TargetContract`),
// а НЕ языковой модели (R37). Заполняет семантика (`=`-тело builtin), читает кодоген.
// Ключ — десугарированный `TypeId` владельца; fallback «структурный контейнер → абстрактный
// builtin» применяет читатель (языковой предикат `BuiltinRegistry::abstractBuiltinOf`).

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "runtime/trust/type_state.hpp" // TypeId/std::hash<TypeId>

namespace trust {

class MethodDelegates {
  public:
    /// Привязка реализации метода builtin: `method` → литеральное C++-имя `delegate`.
    void set(TypeId owner, std::string method, std::string delegate);
    /// Делегат метода у ВЛАДЕЛЬЦА `owner` (без fallback'ов); nullptr — привязки нет.
    [[nodiscard]] const std::string* findByOwner(TypeId owner, std::string_view method) const noexcept;

  private:
    std::unordered_map<TypeId, std::unordered_map<std::string, std::string>> m_byOwner;
};

} // namespace trust