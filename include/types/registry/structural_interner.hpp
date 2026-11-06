#pragma once

// Зона «структурное интернирование»: TypeKey -> TypeId, создание структурных/функциональных/ссылочных
// типов, контейнеров (Array/Tensor/Range/Tuple), template-параметров и инстанциаций.
// Владеет таблицей интернирования. Запись дескрипторов/имён — через приватные примитивы
// TypeStore/TypeNameIndex (friend).

#include "types/group.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

class DiagnosticEngine;
class MethodTable;
class Options;
class TypeNameIndex;
class TypeReadModel;
class TypeStore;

class StructuralInterner {
  public:
    StructuralInterner(const TypeReadModel& read, TypeStore& store, TypeNameIndex& names, DiagnosticEngine& diag, const Options& opts) noexcept;

    /// Предельная глубина вложенности record-инстанциаций (R26). Цикл (`Rec<T>` внутри `Rec` по
    /// значению) ловится отдельно по ключу (template, args); этот лимит — защита от патологической
    /// не-циклической вложенности (переполнение стека компилятора).
    static constexpr std::size_t kMaxRecordInstanceDepth = 256;

    TypeId getOrCreateStructuralType(std::string_view name, TypeKind kind, std::vector<TypeKeyChild> children, std::optional<TypeData> data = std::nullopt);
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
    /// Инстанциация абстрактного нативного шаблона (языковая идентичность: `templateOf` + args).
    /// C++-имя шаблона читается из целевых фактов абстрактного шаблона (не передаётся).
    TypeId getOrCreateNativeTemplateType(TypeId templateOf, std::vector<TypeId> args);

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
    friend class MethodTable;

    /// Оn-demand материализация членов record-специализации (методы/алиасы) из её шаблона.
    /// C++-модель неявной инстанциации: специализация — канонический узел, члены-методы
    /// материализуются при ПЕРВОМ доступе и мемоизируются в дескрипторе. No-op, если тип не
    /// специализация, уже материализован или шаблон не определён.
    /// Доступ закрыт зоной-владельцем: единственный вызывающий — материализатор MethodTable (R26/E1).
    TypeId materializeRecordInstanceMembers(TypeId id);

    /// Кадр стека сборки инстанциаций (R26): (шаблон, типовые аргументы), форма которых
    /// строится В ДАННЫЙ МОМЕНТ. Запись инстанциации в таблицу интернирования происходит ПОСЛЕ
    /// подстановки типов полей, поэтому повторный вход с тем же ключом до регистрации — цикл.
    struct InstanceFrame {
        TypeId templateId;
        std::vector<TypeId> args;
    };

    /// RAII-кадр: добавлен на входе сборки инстанциации, снят при любом выходе (включая
    /// исключение EXPECT), чтобы состояние стека не «протекало» между вызовами (LSP-режим).
    class InstanceScope {
      public:
        InstanceScope(std::vector<InstanceFrame>& stack, InstanceFrame frame)
        : m_stack(stack) {
            m_stack.push_back(std::move(frame));
        }
        ~InstanceScope() { m_stack.pop_back(); }
        InstanceScope(const InstanceScope&) = delete;
        InstanceScope& operator=(const InstanceScope&) = delete;
        InstanceScope(InstanceScope&&) = delete;
        InstanceScope& operator=(InstanceScope&&) = delete;

      private:
        std::vector<InstanceFrame>& m_stack;
    };

    TypeId internStructural(TypeKey key, std::optional<TypeData> data, std::string_view name);
    TypeId substituteElementParam(TypeId elem, TypeId templateFuncType);

    const TypeReadModel& m_read;
    TypeStore& m_store;
    TypeNameIndex& m_names;
    DiagnosticEngine& m_diag;
    const Options& m_opts;
    /// Стек строящихся инстанциаций (R26: защита от циклов и лимит глубины).
    std::vector<InstanceFrame> m_instanceStack;
    /// Ключи циклических инстанциаций, о которых диагностика уже выдана: повторный запрос
    /// (аннотация резолвится несколькими проходами) возвращает INVALID без второй диагностики.
    std::set<std::pair<TypeId, std::vector<TypeId>>> m_failedInstantiationCycles;
    std::unordered_map<TypeKey, TypeId, TypeKeyHash> m_structural;
};

} // namespace trust
