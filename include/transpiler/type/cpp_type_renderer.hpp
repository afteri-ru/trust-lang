#pragma once

// C++-рендеринг имени типа для бэкенда (R37): целевое C++-представление — не факт языковой
// модели. Соседствует с `TypeNameResolver` (codegen-политика), но НЕ сливается с ним (MEMORY
// types/registry). Читает форму через `TypeReadModel` (публичные аксессоры) и целевые факты через
// порт `TypeCppFactsView` (поля дескриптора читаются только аксессорами `TypeStore`, R49).

#include "transpiler/type/type_cpp_facts_view.hpp"
#include "types/type_read.hpp"
#include "types/type_id.hpp"

#include <expected>
#include <string>

namespace trust {

/// C++-представление значения типа (полное, qualified). Отказ без молчаливого fallback:
/// тип без C++-представления даёт std::unexpected с причиной.
class CppTypeRenderer {
  public:
    CppTypeRenderer(const TypeReadModel& types, const TypeCppFactsView& facts) noexcept
    : m_types(types)
    , m_facts(facts) {}

    [[nodiscard]] std::expected<std::string, std::string> render(TypeId id) const;
    [[nodiscard]] std::expected<std::string, std::string> render(QualType state) const;

  private:
    /// View по значению: `Context::readTypes()` возвращает `TypeReadModel` временным объектом.
    TypeReadModel m_types;
    TypeCppFactsView m_facts;
};

} // namespace trust
