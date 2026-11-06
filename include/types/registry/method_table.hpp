#pragma once

// Зона «методы типа»: наборы сигнатур (перегрузки) по структурному ключу MethodKey,
// алиасы имён и поиск статических членов.
// Строковые trust-spelling overloads — граница разбора (делегируют MethodKey-версиям);
// внутри зоны флаги из имени не извлекаются.
// Владеет записью методов в дескриптор типа (friend TypeStore); чтение — публичные методы зоны.

#include "types/call/binders.hpp"
#include "types/call/callable_info.hpp"
#include "types/registry/method_ref.hpp"
#include "types/type/method_key.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {

class FunctionIdentity;
class StructuralInterner;
class TypeStore;

class MethodTable {
  public:
    MethodTable(TypeStore& store, const FunctionIdentity& identity) noexcept
    : m_store(store)
    , m_identity(identity) {}

    /// Регистрация trust-метода по spelling: имя — ТОЛЬКО trust, `^` → const, `::` → static.
    /// Нативность в имени не кодируется: для C++-проекции (builtin-контейнеры) признак записи
    /// выводится из дескриптора владельца (native class/template) или static-ключа.
    void addMethod(TypeId type, std::string_view trustSpelling, TypeId funcType, std::vector<std::string_view> aliases = {}, CallableParamInfo params = {},
                   TemplateBinders binders = {}, Visibility access = Visibility::Public, DeclId decl = {});

    /// Регистрация по готовому ключу (AST-путь: `^` уже в attr::ReadOnly).
    /// `isNative` ВЫВОДИТСЯ из дескриптора владельца (native class/template) или `isStatic`.
    void addMethod(TypeId type, MethodKey key, TypeId funcType, std::vector<MethodKey> aliases = {}, CallableParamInfo params = {},
                   TemplateBinders binders = {}, Visibility access = Visibility::Public, DeclId decl = {});

    /// Регистрация метода, объявленного ШИРЕ ядрового интерфейса ВСТРОЕННОГО типа (`=`-тело
    /// модуля). Дескриптор встроенного типа иммутабелен (ядро), поэтому метод попадает в
    /// per-instance overlay и участвует в поиске наравне с ядровым интерфейсом.
    void addBuiltinExtraMethod(TypeId type, MethodKey key, TypeId funcType, CallableParamInfo params = {});

    /// Очистка per-instance overlay методов встроенных типов (reset() реестра).
    void clearBuiltinExtraMethods() noexcept;

    /// Поиск метода по trust-spelling (разбор на границе) или готовому ключу.
    [[nodiscard]] std::optional<MethodRef> findMethodInfo(TypeId type, std::string_view trustSpelling) const;
    [[nodiscard]] std::optional<MethodRef> findMethodInfo(TypeId type, MethodKey key) const;

    /// Внедряет материализатор членов record-специализаций (невладеющий указатель; ставится
    /// композиционным корнем ПОСЛЕ построения всех зон из-за порядка конструирования).
    void setMemberMaterializer(StructuralInterner* materialize) noexcept { m_materializer = materialize; }

    /// Точная сигнатура уже зарегистрирована на СОБСТВЕННОМ дескрипторе типа (без базовых).
    [[nodiscard]] bool hasOwnMethodSignature(TypeId type, std::string_view trustSpelling, TypeId signature, TemplateBinders binders = {}) const;
    [[nodiscard]] bool hasOwnMethodSignature(TypeId type, MethodKey key, TypeId signature, TemplateBinders binders = {}) const;

    /// Есть ли на собственном дескрипторе член с тем же bare-именем и константностью.
    [[nodiscard]] bool hasOwnMemberName(TypeId type, std::string_view trustSpelling) const;
    [[nodiscard]] bool hasOwnMemberName(TypeId type, MethodKey key) const;

    /// Статический член нативного класса по последнему сегменту имени.
    [[nodiscard]] std::vector<TypeId> findStaticMethod(TypeId type, std::string_view name) const;

  private:
    /// Единая реализация: key уже без '%', isNative — ВЫЧИСЛЕННЫЙ вызывающим признак записи.
    void addMethodImpl(TypeId type, MethodKey key, bool isNative, TypeId funcType, std::vector<MethodKey> aliases, CallableParamInfo params,
                       TemplateBinders binders, Visibility access = Visibility::Public, DeclId decl = {});

    /// Поиск метода в per-instance overlay встроенного типа (методы, объявленные ШИРЕ ядрового интерфейса).
    [[nodiscard]] std::optional<MethodRef> findInBuiltinExtras(TypeId builtinId, const MethodKey& want) const;

    TypeStore& m_store;
    const FunctionIdentity& m_identity;
    /// Материализатор членов record-специализаций (см. setMemberMaterializer).
    StructuralInterner* m_materializer = nullptr;
    /// Методы ВСТРОЕННЫХ типов, объявленные ШИРЕ ядрового интерфейса (per-instance overlay;
    /// собственный дескриптор встроенного типа неизменяем). Ключ — канонический TypeId builtin.
    std::unordered_map<TypeId, TypeDescriptor> m_builtinExtras;
};

} // namespace trust
