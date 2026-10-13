#pragma once

// include/types/registry/method_table.hpp
// Зона «методы типа»: наборы сигнатур (перегрузки) по полному ключу (нативность '%',
// константность '^'), алиасы имён и поиск статических членов.
//
// Владеет записью методов в дескриптор типа (friend TypeStore); чтение — публичные методы зоны.

#include "types/call/binders.hpp"
#include "types/call/callable_info.hpp"
#include "types/registry/method_ref.hpp"
#include "types/type_id.hpp"

#include <optional>
#include <string_view>
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

    /// Регистрирует метод (перегрузки добавляются к набору ключа; точный C++-дубль — EXPECT).
    void addMethod(TypeId type, std::string_view name, TypeId funcType, std::vector<std::string_view> aliases = {}, CallableParamInfo params = {},
                   TemplateBinders binders = {});

    /// Поиск метода по доверенному имени (нормализация срезом '%'/'^').
    /// Перед поиском материализует члены record-специализации по требованию (см.
    /// StructuralInterner::materializeRecordInstanceMembers); повторный доступ — no-op (мемо).
    [[nodiscard]] std::optional<MethodRef> findMethodInfo(TypeId type, std::string_view name) const;

    /// Внедряет материализатор членов record-специализаций (невладеющий указатель; ставится
    /// композиционным корнем ПОСЛЕ построения всех зон из-за порядка конструирования).
    void setMemberMaterializer(StructuralInterner* materialize) noexcept { m_materializer = materialize; }

    /// Точная сигнатура уже зарегистрирована на СОБСТВЕННОМ дескрипторе типа (без базовых).
    [[nodiscard]] bool hasOwnMethodSignature(TypeId type, std::string_view name, TypeId signature, TemplateBinders binders = {}) const;

    /// Есть ли на собственном дескрипторе член с тем же bare-именем и константностью.
    [[nodiscard]] bool hasOwnMemberName(TypeId type, std::string_view name) const;

    /// Статический член нативного класса по последнему сегменту имени.
    [[nodiscard]] std::vector<TypeId> findStaticMethod(TypeId type, std::string_view name) const;

  private:
    TypeStore& m_store;
    const FunctionIdentity& m_identity;
    /// Материализатор членов record-специализаций (см. setMemberMaterializer).
    StructuralInterner* m_materializer = nullptr;
};

} // namespace trust
