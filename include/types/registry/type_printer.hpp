#pragma once

// include/types/registry/type_printer.hpp
// ЕДИНЫЙ принтер имени типа с политикой (аналог Clang `TypePrinter` + `PrintingPolicy`).
// Одна реализация покрывает номинальное имя (Trust) и C++-представление значения; молчаливого
// fallback-имени нет - непредставимый тип возвращает std::unexpected с причиной.
//
// Политики:
//   * kTrust      - как тип пишется в языке Trust (`unique<Int32>`, `Array<Int32>`, `MyInt`);
//   * kCpp        - C++-представление значения (`std::int32_t`, `trust::Unique<int32_t>`);
//   * kDiagnostic - имя для диагностик (форма kTrust) с опциональным раскрытием алиасов (aka).
//
// Зона не имеет собственного состояния: читает дескрипторы (TypeStore), реализации встроенных
// типов (BuiltinRegistry) и провайдер модульных имён (внедряется композиционным корнем).

#include "location/module_index.hpp"
#include "types/type_id.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace trust {

class BuiltinRegistry;
class ModuleNameLookup;
class TypeStore;

enum class TypePrintTarget : uint8_t {
    kTrust,
    kCpp,
    kDiagnostic,
};

/// Пространство имён в имени (аналог Clang PrintingPolicy::SuppressScope):
///   * kUnqualified - короткое имя (`Point`, `c_Point`);
///   * kQualified   - с namespace-путём (`ns::Point`, `c_ns::c_Point`).
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
///   * `INVALID_TYPE_ID` (нет типа) -> `<no type>` для kTrust/kDiagnostic; для kCpp - отказ
///     (C++-представления у отсутствующего типа нет);
///   * структурный тип без НОМИНАЛЬНОГО имени (кортеж, сигнатура) -> `<unnamed type #<id>>`;
///   * id с kind, но без дескриптора -> `std::unexpected` с причиной (нарушение инварианта).
///
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

    /// Провайдер логического имени модуля (невладеющий; nullptr — модульные имена не печатались).
    void setModuleNames(const ModuleNameLookup* names) noexcept { m_moduleNames = names; }

    [[nodiscard]] const TypeStore& store() const noexcept { return m_store; }
    [[nodiscard]] const BuiltinRegistry& builtins() const noexcept { return m_builtins; }
    [[nodiscard]] const ModuleNameLookup* moduleNames() const noexcept { return m_moduleNames; }

    /// C++-namespace модуля (`<enc>`); FAULT, если провайдер не внедрён.
    [[nodiscard]] std::string moduleCppNamespace(ModuleIndex index) const;

    /// Номинальное имя типа (Trust-форма). Отсутствие дескриптора — FAULT.
    [[nodiscard]] std::string getTypeName(TypeId id) const;
    /// C++-имя (всегда qualified) identity/QualType; отказ — std::unexpected с причиной.
    [[nodiscard]] std::expected<std::string, std::string> getCppTypeName(TypeId id) const;
    [[nodiscard]] std::expected<std::string, std::string> getCppTypeName(QualType state) const;

  private:
    const TypeStore& m_store;
    const BuiltinRegistry& m_builtins;
    const ModuleNameLookup* m_moduleNames = nullptr;
};

} // namespace trust
