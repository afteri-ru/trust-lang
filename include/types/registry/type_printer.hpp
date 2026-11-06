#pragma once

// ЕДИНЫЙ принтер имени типа с политикой (аналог Clang `TypePrinter` + `PrintingPolicy`).
// Одна реализация покрывает номинальное имя (Trust) и C++-представление значения; молчаливого
// fallback-имени нет - непредставимый тип возвращает std::unexpected с причиной.
// Политики:
//   * kTrust      - как тип пишется в языке Trust (`unique<Int32>`, `Array<Int32>`, `MyInt`);
//   * kDiagnostic - имя для диагностик (форма kTrust) с опциональным раскрытием алиасов (aka).
// C++-представление значения в принтер НЕ входит (R37): рендеринг C++-имени —
// `transpiler/type/cpp_type_renderer.hpp`.
// Зона не имеет собственного состояния: читает дескрипторы (TypeStore) и реализации встроенных
// типов (BuiltinRegistry).

#include "types/type_id.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace trust {

class BuiltinRegistry;
class TypeStore;

enum class TypePrintTarget : uint8_t {
    kTrust,
    kDiagnostic,
};

/// Пространство имён в имени (аналог Clang PrintingPolicy::SuppressScope):
///   * kUnqualified - короткое имя (`Point`);
///   * kQualified   - с namespace-путём (`ns::Point`).
/// Для типов без namespace-пути проекции совпадают; file-private типы всегда короткие
/// (C++ anonymous namespace).
enum class TypePrintScope : uint8_t {
    kUnqualified,
    kQualified,
};

struct TypePrintPolicy {
    TypePrintTarget target{TypePrintTarget::kTrust};
    /// Только для kDiagnostic: показать десугар алиаса (`MyInt (aka Int32)`).
    bool showDesugared{false};
    /// Скоуп имени (kQualified - с namespace-путём); не влияет на builtin/native C++-имена.
    TypePrintScope scope{TypePrintScope::kUnqualified};
};

/// Имя типа по политике. Контракт печати (без silent fallback):
///   * `INVALID_TYPE_ID` (нет типа) -> `<no type>` для kTrust/kDiagnostic;
///   * структурный тип без НОМИНАЛЬНОГО имени (кортеж, сигнатура) -> `<unnamed type #<id>>`;
///   * id с kind, но без дескриптора -> `std::unexpected` с причиной (нарушение инварианта).
/// Перегрузка от `TypeId` печатает IDENTITY (qualifier-биты игнорируются); квалификаторы
/// (const) учитывает только перегрузка от `QualType`.
std::expected<std::string, std::string> printType(const class TypePrinter& printer, TypeId id, TypePrintPolicy policy = {});
std::expected<std::string, std::string> printType(const class TypePrinter& printer, QualType state, TypePrintPolicy policy = {});

/// Зона печати: политики + единый вход для потребителей.
class TypePrinter {
  public:
    TypePrinter(const TypeStore& store, const BuiltinRegistry& builtins) noexcept
    : m_store(store)
    , m_builtins(builtins) {}

    [[nodiscard]] const TypeStore& store() const noexcept { return m_store; }
    [[nodiscard]] const BuiltinRegistry& builtins() const noexcept { return m_builtins; }

    /// Номинальное имя типа (Trust-форма). Отсутствие дескриптора — FAULT.
    [[nodiscard]] std::string typeNameOf(TypeId id) const;

  private:
    const TypeStore& m_store;
    const BuiltinRegistry& m_builtins;
};

} // namespace trust
