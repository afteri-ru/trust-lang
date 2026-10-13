// include/types/registry/structural_interner.hpp
#pragma once

// Зона «структурное интернирование»: TypeKey -> TypeId, создание структурных/функциональных/ссылочных
// типов, контейнеров (Array/Tensor/Range/Tuple), template-параметров и инстанциаций.
//
// Владеет таблицей интернирования. Запись дескрипторов/имён — через приватные примитивы
// TypeStore/TypeNameIndex (friend).

#include "types/group.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

class DiagnosticEngine;
class Options;
class TypeNameIndex;
class TypeReadModel;
class TypeStore;

class StructuralInterner {
  public:
    StructuralInterner(const TypeReadModel& read, TypeStore& store, TypeNameIndex& names, DiagnosticEngine& diag, const Options& opts) noexcept;

    TypeId getOrCreateStructuralType(std::string_view name, TypeKind kind, std::vector<TypeId> children, std::optional<TypeData> data = std::nullopt,
                                     std::string_view preprocInclude = {}, std::vector<std::string> names = {});
    TypeId getOrCreateFunctionType(TypeId returnType, std::vector<TypeId> paramTypes, TypeId variadicType = INVALID_TYPE_ID, bool hasTrust = false,
                                   TypeId paramPackType = INVALID_TYPE_ID, uint8_t paramPackPosition = 0);
    TypeId getOrCreateTupleType(std::vector<std::pair<std::string, QualType>> elements);
    TypeId getOrCreateRangeType(TypeId elementType);
    TypeId getOrCreateArrayType(TypeId elementType, std::vector<uint64_t> dimensions = {});
    TypeId getOrCreateTensorType(TypeId elementType, std::vector<uint64_t> shape = {}, bool sparse = false);
    TypeId getOrCreateRefType(RefType kind, TypeId pointee, TypeId deleter = INVALID_TYPE_ID, TypeId accessPolicy = INVALID_TYPE_ID,
                              TypeId impl = INVALID_TYPE_ID);
    TypeId getOrCreatePackExpansionType(TypeId pattern);
    TypeId getOrCreateTemplateParamType(std::string_view name, TypeId owner = INVALID_TYPE_ID, uint32_t depth = 0, uint32_t index = 0);
    TypeId getOrCreateRecordTemplateInstance(TypeId templateId, std::vector<TypeId> args);

    /// Оn-demand материализация членов record-специализации (методы/алиасы) из её шаблона.
    /// C++-модель неявной инстанциации: специализация — канонический узел, члены-методы
    /// материализуются при ПЕРВОМ доступе и мемоизируются в дескрипторе. No-op, если тип не
    /// специализация, уже материализован или шаблон не определён. Вызывается MethodTable
    /// (возврат игнорируется).
    TypeId materializeRecordInstanceMembers(TypeId id);
    TypeId getOrCreateNativeTemplateType(std::string_view cppTemplate, std::vector<TypeId> args, std::string_view preprocInclude = {});

    TypeId applyRefType(TypeId base, RefType kind);
    QualType applyRefType(QualType base, RefType kind);
    TypeId applyRefType(TypeId base, RefType kind, TypeId deleter, TypeId accessPolicy = INVALID_TYPE_ID, TypeId impl = INVALID_TYPE_ID);
    QualType applyRefType(QualType base, RefType kind, TypeId deleter, TypeId accessPolicy = INVALID_TYPE_ID, TypeId impl = INVALID_TYPE_ID);

    TypeId instantiateRangeMethod(TypeId objType, TypeId templateFuncType);
    TypeId instantiateArrayMethod(TypeId objType, TypeId templateFuncType);
    TypeId instantiateTensorMethod(TypeId objType, TypeId templateFuncType);
    TypeId instantiateNativeTemplateMethod(TypeId objType, TypeId templateFuncType);

    /// Стартовая таблица интернирования из BuiltinTypeCore (seed-структурные типы). Вызывается
    /// композиционным корнем (ctor/reset), не потребителями.
    void reset(const std::unordered_map<TypeKey, TypeId, TypeKeyHash>& seed);
    /// Изъятие таблицы интернирования (построение общего ядра в seed-режиме).
    std::unordered_map<TypeKey, TypeId, TypeKeyHash> takeStructuralTable();

  private:
    TypeId internStructural(TypeKey key, std::optional<TypeData> data, std::string_view name, std::string_view preprocInclude);
    TypeId substituteElementParam(TypeId elem, TypeId templateFuncType);

    const TypeReadModel& m_read;
    TypeStore& m_store;
    TypeNameIndex& m_names;
    DiagnosticEngine& m_diag;
    const Options& m_opts;
    std::unordered_map<TypeKey, TypeId, TypeKeyHash> m_structural;
};

} // namespace trust
