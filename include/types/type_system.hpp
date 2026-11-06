#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "types/call/binders.hpp"
#include "types/registry/builtin_registry.hpp"
#include "types/registry/builtin_type_core.hpp"
#include "types/call/callable_info.hpp"
#include "types/func/fn_identity.hpp"
#include "types/group.hpp"
#include "types/registry/named_type_registrar.hpp"
#include "types/registry/method_table.hpp"
#include "types/names/storage_key.hpp"
#include "types/registry/structural_interner.hpp"
#include "types/type_id.hpp"
#include "types/registry/type_identity_scope.hpp"
#include "types/registry/type_name_index.hpp"
#include "types/registry/type_code_cache.hpp"
#include "types/registry/type_printer.hpp"
#include "types/registry/type_store.hpp"
#include "utils/string_view_hash.hpp"
#include "attrs/attr.hpp"
#include "location/location.hpp"
#include "types/type_data.hpp"
#include "types/type_read.hpp"

namespace trust {

// Forward declarations
class DiagnosticEngine;
class FunctionIdentity;
class Options;

class TypeSystem {
  public:
    explicit TypeSystem(DiagnosticEngine& diag, const Options& opts);

    /// Сброс к builtin-состоянию (пользовательские/структурные типы очищаются); вызывается
    /// на каждый запуск семантики. ЕДИНСТВЕННЫЙ вызывающий — прогон семантики; смешение
    /// «reset() + новый экземпляр» запрещено (E1/R7). Инкрементирует поколение реестра.
    void reset();

    /// Поколение реестра: снимок (`SemanticModel`) действителен, пока поколение не изменилось.
    [[nodiscard]] std::uint64_t generation() const noexcept { return m_generation; }

    // Корень владеет зонами и lifecycle; саму операцию выполняет зона-владелец.
    // Чтение потребителями — через TypeReadModel.
    [[nodiscard]] TypeStore& store() noexcept { return m_store; }
    [[nodiscard]] const TypeStore& store() const noexcept { return m_store; }
    [[nodiscard]] TypeNameIndex& names() noexcept { return m_names; }
    [[nodiscard]] const TypeNameIndex& names() const noexcept { return m_names; }
    [[nodiscard]] NamedTypeRegistrar& registrar() noexcept { return m_registrar; }
    [[nodiscard]] const NamedTypeRegistrar& registrar() const noexcept { return m_registrar; }
    [[nodiscard]] StructuralInterner& interner() noexcept { return m_interner; }
    [[nodiscard]] MethodTable& methods() noexcept { return m_methods; }
    [[nodiscard]] const MethodTable& methods() const noexcept { return m_methods; }
    [[nodiscard]] BuiltinRegistry& builtins() noexcept { return m_builtins; }
    [[nodiscard]] const BuiltinRegistry& builtins() const noexcept { return m_builtins; }
    [[nodiscard]] const TypePrinter& printer() const noexcept { return m_printer; }
    /// Кеш канонических кодов типов (per-instance, очищается `reset()`).
    [[nodiscard]] TypeCodeCache& typeCodes() const noexcept { return m_codes; }
    /// Единая точка C++-идентичности сигнатур (читает зоны; состояния не имеет).
    [[nodiscard]] const FunctionIdentity& identity() const noexcept { return m_identity; }
    /// Read-модель над зонами (capability-narrowing для чтения).
    [[nodiscard]] const TypeReadModel& read() const noexcept { return m_read; }

    // Собираются семантикой из ВСЕХ проанализированных объявлений, ВКЛЮЧАЯ модули, пришедшие
    // импортом: их AST не входит в корневой AST программы, поэтому borrow-анализ берёт метки
    // отсюда, а не только из обхода корня. Имена — «как написаны» (raw), нормализация методов —
    // на стороне потребителя.
    void markBorrowedClass(std::string name) { m_borrowedClasses.insert(std::move(name)); }
    void markBorrowedMethod(std::string name) { m_borrowedMethods.insert(std::move(name)); }
    [[nodiscard]] const std::set<std::string>& borrowedClasses() const noexcept { return m_borrowedClasses; }
    [[nodiscard]] const std::set<std::string>& borrowedMethods() const noexcept { return m_borrowedMethods; }

    // Пакет — ОТДЕЛЬНЫЙ структурный узел PackExpansion(pattern), а НЕ флаг в
    // TemplateParamTypeData: TypeKey не содержит TypeData, поэтому флаг в data не различал бы
    // `T` и `T...`. Group::kEllipsis занят runtime-вариадиками (`...` в вызовах/прототипах),
    // поэтому типовая pack-expansion имеет собственную группу Group::kPackExpansion.

    // Интернирование — зона StructuralInterner (`interner().getOrCreate*`); алгоритмы
    // подстановки — `types/subst` (принимают TypeWriteContext{read, interner}).

    // Регистрация абстрактного шаблона — зона NamedTypeRegistrar (registerNativeTemplate).

    // Регистрация — зона NamedTypeRegistrar (registerNativeClass); C++-struct НЕ генерируется
    // (cppName + include on-use), методы — MethodTable::addMethod.
  private:
    friend class NamedTypeRegistrar;
    friend class MethodTable;
    friend class StructuralInterner;

    /// Возвращает общее иммутабельное ядро встроенных типов (ленивый, thread-safe).
    /// Строитель ядра — зона BuiltinCoreBuilder; корень только разделяет результат.
    static const BuiltinTypeCore& builtinCore();

    DiagnosticEngine& m_diag;
    const Options& m_opts;

    /// Зоны состояния (единственные владельцы): дескрипторы/identity — TypeStore, имена — TypeNameIndex.
    /// Объявлены ДО зависимых зон; конструктор связывает их (TypeNameIndex читает TypeStore).
    TypeStore m_store;
    TypeNameIndex m_names;
    /// Зона встроенных определений/рантайм-символов (владеет per-instance таблицами).
    /// Объявлена до печати: принтер читает реализации встроенных типов.
    BuiltinRegistry m_builtins;
    /// Зона печати имён (projection only); читает TypeStore/BuiltinRegistry + провайдер модулей.
    TypePrinter m_printer;
    /// Кеш канонических кодов типов (владелец — корень; коды вычисляет FunctionIdentity).
    mutable TypeCodeCache m_codes;
    /// Идентичность сигнатур: читает зоны, состояния не имеет.
    FunctionIdentity m_identity;
    /// Зона создания именованных типов (транзакции reserve→push→bind); ссылается на контекст.
    NamedTypeRegistrar m_registrar;
    /// Зона методов типа (запись — friend TypeStore/TypeSystem); читает TypeStore и идентичность.
    MethodTable m_methods;
    /// Read-модель над зонами (view; используется зонами, которым нужны только чтения).
    TypeReadModel m_read;
    /// Зона структурного интернирования (владеет TypeKey-таблицей); пишет через store/names.
    StructuralInterner m_interner;

    /// @[borrowed]-метки объявлений (см. одноимённые методы выше); очищаются reset().
    std::set<std::string> m_borrowedClasses;
    std::set<std::string> m_borrowedMethods;

    /// Поколение реестра: инкрементируется КАЖДЫМ `reset()` (единственная точка инвалидации
    /// производных снимков). Читатели снимка (`SemanticModel`) сверяют поколение и падают
    /// (`EXPECT`) при обращении к устаревшему — молчаливые данные запрещены (R7).
    std::uint64_t m_generation = 0;
};

} // namespace trust

// Предикаты категории типа определены через полный TypeSystem, поэтому включаются после класса.
#include "types/registry/type_predicates.hpp"
