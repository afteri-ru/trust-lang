// Целевые C++-факты ТИПА (C++-имя, целевые `#include`, библиотеки линковки, C++-имя нативного
// шаблона) — ЕДИНСТВЕННЫЙ владелец (R1/R37). Владелец — per-run `TargetContract` (компонент
// `target`), а НЕ языковая модель (`types`). Ключ — КАНОНИЧЕСКИЙ `TypeId` (канонизация — у
// вызывающего через `TypeStore::cppFactsKeyOf`; `target` не зависит от `types`).
//
// Seed встроенных интринсиков (Bool/Int*/UInt*/Float32/64/CString) объявляется статической таблицей
// (см. `target::builtinCppSeed()`); `Pipeline` связывает её «имя↔TypeId» и засевает контракт.
//

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "runtime/trust/type_state.hpp" // TypeId/std::hash<TypeId> — единый ABI-заголовок

namespace trust {

/// Целевые C++-факты одного типа. Отдельного статуса реализации нет: факты пишет либо seed
/// встроенного типа, либо загрузка реализации (`Name = %cpp { ... };`), либо объявление
/// нативного класса/шаблона.
struct CppTypeFacts {
    std::string cppName;                      ///< C++-имя типа (пусто — C++-имя решает рендерер)
    std::vector<std::string> preprocIncludes; ///< директивы препроцессора; первый — основной
    std::vector<std::string> libs;            ///< логические библиотеки `@[link("name")]` (on-use)
    std::string cppTemplate;                  ///< C++-имя нативного шаблона (пусто — не шаблон)
};

/// Строка seed-таблицы встроенных интринсиков: trust-имя → целевые C++-факты.
struct BuiltinCppSeedEntry {
    std::string_view name;
    std::string_view cppName;
    std::vector<std::string_view> includes;
};

/// Статическая seed-таблица целевых фактов встроенных интринсиков (владелец — `target`).
[[nodiscard]] const std::vector<BuiltinCppSeedEntry>& builtinCppSeed();

/// Хранилище целевых C++-фактов типа (карта `TypeId → CppTypeFacts`). Per-run.
class TypeCppFacts {
  public:
    /// Факты реализации/объявления (user) — «загружена реализация». Помечает ключ как user.
    void set(TypeId id, CppTypeFacts facts);
    /// Seed-факты встроенного интринсика — НЕ помечают ключ как user (предикат `hasUser`).
    void seed(TypeId id, CppTypeFacts facts);

    [[nodiscard]] const CppTypeFacts* factsOf(TypeId id) const noexcept;
    /// Есть ли ПОЛЬЗОВАТЕЛЬСКАЯ реализация (не seed) — предикат «реализовано» (бывший isImplemented).
    [[nodiscard]] bool hasUser(TypeId id) const noexcept;
    [[nodiscard]] std::string_view cppNameOf(TypeId id) const noexcept;

  private:
    std::unordered_map<TypeId, CppTypeFacts> m_byType;
    std::unordered_set<TypeId> m_user;
};

} // namespace trust
