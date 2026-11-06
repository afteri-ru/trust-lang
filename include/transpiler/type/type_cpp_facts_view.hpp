#pragma once

// Порт чтения целевых C++-фактов ТИПА на стороне кодогена (R37). Факты живут ВНЕ языковой
// модели — в компоненте `target` (`TargetContract::typeCppFacts()`); здесь — ЕДИНЫЙ аксессор
// для кодогена, объединяющий карту фактов с ЯЗЫКОВЫМИ предикатами адресации `types`
// (канонизация `TypeStore::cppFactsKeyOf`, абстрактный контейнер `BuiltinRegistry::abstractBuiltinOf`).
// Аксессоры `types`, объединённые портом (семантика читает факты отдельно — `semantic` не зависит от `transpiler`):
//   * `declaredCppName` — имя из фактов по каноническому ключу (без fallback'ов);
//   * `effective` — факты типа, иначе абстрактного нативного шаблона (`templateOf`), иначе
//     абстрактного builtin-типа структурного контейнера (`Array<T>`→`:Array` и т.п.);
//   * `preprocIncludesOf`/`linkLibsOf` — из `effective` (плюс языковой спец-случай `std::function`).
// Читает `types` только через публичный `type_read.hpp` (без прямого include internal-подкаталогов).

#include "target/type_cpp_facts.hpp"
#include "types/type_id.hpp"
#include "types/type_read.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust {

class TypeCppFactsView {
  public:
    TypeCppFactsView(const TypeReadModel& types, const TypeCppFacts& facts) noexcept
    : m_types(types)
    , m_facts(facts) {}

    /// C++-имя из фактов по каноническому ключу (пусто — рендерер решает номинально/структурно).
    [[nodiscard]] std::string_view declaredCppName(TypeId id) const noexcept;
    /// C++-имя нативного шаблона (по владельцу `templateOf`), пусто — не нативный шаблон.
    [[nodiscard]] std::string_view nativeTemplateCppName(TypeId id) const noexcept;
    /// Эффективные факты: тип → абстрактный нативный шаблон → абстрактный builtin-контейнер.
    [[nodiscard]] const CppTypeFacts* effective(TypeId id) const noexcept;
    /// Заголовки include типа (из `effective`; спец-случай `std::function` для сигнатур).
    [[nodiscard]] const std::vector<std::string>& preprocIncludesOf(TypeId id) const noexcept;
    [[nodiscard]] std::string_view preprocIncludeOf(TypeId id) const noexcept;
    /// Логические библиотеки (`@[link]`) типа (из `effective`).
    [[nodiscard]] std::vector<std::string> linkLibsOf(TypeId id) const;
    /// Есть ли у типа ПОЛЬЗОВАТЕЛЬСКАЯ реализация (не seed) — предикат «реализовано».
    [[nodiscard]] bool isImplemented(TypeId id) const noexcept;
    /// C++-имя материализуемого `Any` из фактов реализации; отсутствие реализации — FAULT
    /// (provider-gate обязан отработать до кодогена; fallback-имени нет).
    [[nodiscard]] std::string_view anyCppName() const;

  private:
    TypeReadModel m_types;
    const TypeCppFacts& m_facts;
};

} // namespace trust