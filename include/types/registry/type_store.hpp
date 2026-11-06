#pragma once

// Зона «хранение типов»: дескрипторы пользовательских типов, объявленные-но-не-определённые
// record-типы, выдача registry_index, маршрутизация builtin/user и ПРОЕКЦИИ дескриптора
// (канонизация/identity, поля/базы, namespace/module).
// Владение: TypeStore — единственный владелец этих данных. Запись — ТОЛЬКО через приватные
// примитивы, доступные друзьям-оркестраторам (TypeSystem/NamedTypeRegistrar/
// StructuralInterner/MethodTable). Публично доступно только чтение.
// Дескрипторы хранятся в std::deque: адреса элементов стабильны при росте (как
// sourcemap::FileEntry и SymbolArena), поэтому `descriptorOf()`/`lookup()` возвращают указатель,
// валидный до `resetToBuiltin()`.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "location/location.hpp"
#include "location/module_index.hpp"
#include "types/registry/builtin_type_core.hpp"
#include "types/names/storage_key.hpp"
#include "types/type/ref_type.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"
#include "types/registry/type_identity_scope.hpp"

namespace trust {

class TypeStore {
  public:
    TypeStore() = default;

    /// Имя типа (canonical/display): единственный аксессор имени дескриптора вне `types` (E1/R49).
    [[nodiscard]] std::string_view nameOf(TypeId id) const noexcept;
    /// Методы типа (`obj.method`): структурный ключ → запись; nullptr — дескриптора нет. Read-only view.
    [[nodiscard]] const std::map<MethodKey, MethodInfo>* methodsOf(TypeId id) const noexcept;
    /// Алиасы методов (bare-имя → ключ цели); nullptr — дескриптора нет. Read-only view.
    [[nodiscard]] const std::map<std::string, MethodKey>* methodAliasesOf(TypeId id) const noexcept;
    /// Непосредственная цель алиаса (`A → B → Int32`); INVALID — тип не алиас.
    [[nodiscard]] TypeId baseTypeOf(TypeId id) const noexcept;
    /// Есть ли дескриптор для id (встроенный или зарегистрированный пользовательский).
    [[nodiscard]] bool hasDescriptor(TypeId id) const noexcept { return descriptorOf(id) != nullptr; }
    /// Число зарегистрированных пользовательских типов (адреса дескрипторов не выдаются, E1/R49).
    [[nodiscard]] std::size_t userTypeCount() const noexcept { return m_descriptors.size(); }

    [[nodiscard]] const std::optional<TypeData>& getTypeData(TypeId id) const noexcept;
    template <typename T>
    [[nodiscard]] const T* getTypeDataAs(TypeId id) const noexcept {
        const auto& data = getTypeData(id);
        if (data && std::holds_alternative<T>(*data)) {
            return &std::get<T>(*data);
        }
        return nullptr;
    }
    [[nodiscard]] bool isTypeDataKind(TypeId id, TypeDataKind kind) const noexcept;
    [[nodiscard]] bool isCompleteType(TypeId id) const noexcept;
    [[nodiscard]] bool isForwardDecl(TypeId id) const noexcept;

    [[nodiscard]] MapperRange getTypeSourceRange(TypeId id) const;
    /// Форма record-типа: поля в порядке объявления; nullptr — форма не материализована (не record либо
    /// forward-объявление, см. `isForwardDecl`). Единственный аксессор формы вне `types` (E1/R1).
    [[nodiscard]] const std::vector<TupleElementData>* fieldsOf(TypeId id) const noexcept;
    /// Базы класса (тип + доступ); пустой набор — баз нет (не-record тоже пуст).
    [[nodiscard]] const std::vector<BaseClass>& basesOf(TypeId id) const noexcept;
    /// Инстанциация record-шаблона: шаблон + типовые аргументы; nullopt — не инстанциация.
    struct RecordInstanceView {
        TypeId templateOf{INVALID_TYPE_ID};
        const std::vector<TypeId>& templateArgs;
    };
    [[nodiscard]] std::optional<RecordInstanceView> instanceOf(TypeId id) const noexcept;
    [[nodiscard]] QualType findField(TypeId type, std::string_view name) const noexcept;

    /// Поле record-типа: объявленный тип, C++-доступ и тип-ВЛАДЕЛЕЦ (объявление поля с обходом
    /// базовых классов). ЕДИНЫЙ обход для `findField` и проверки доступа к члену.
    struct FieldAccessInfo {
        TypeId owner{INVALID_TYPE_ID};
        QualType type;
        Visibility access{Visibility::Public};
    };
    [[nodiscard]] std::optional<FieldAccessInfo> findFieldAccess(TypeId type, std::string_view name) const noexcept;

    [[nodiscard]] std::string_view namespacePath(TypeId id) const;
    [[nodiscard]] ModuleIndex moduleOf(TypeId id) const;
    [[nodiscard]] bool isFilePrivate(TypeId id) const;

    [[nodiscard]] TypeId desugaredTypeId(TypeId id) const noexcept;
    [[nodiscard]] TypeId semanticTypeId(TypeId id) const noexcept;
    [[nodiscard]] TypeId identityOf(TypeId id, TypeIdentityScope scope) const;
    [[nodiscard]] TypeId getBaseType(TypeId id) const noexcept;
    [[nodiscard]] TypeId getPointeeType(TypeId id) const noexcept;
    [[nodiscard]] bool typesEqual(TypeId a, TypeId b) const;

    [[nodiscard]] bool isBuiltinType(TypeId id) const noexcept;
    [[nodiscard]] bool isUserDefinedType(TypeId id) const noexcept;
    [[nodiscard]] const BuiltinTypeCore* builtinCore() const noexcept { return m_builtin; }

    /// Объявленный record: типовые параметры до `defineRecord` (nullptr — не объявлен).
    [[nodiscard]] const std::vector<TypeId>* declaredRecordParams(TypeId id) const noexcept;
    [[nodiscard]] bool hasDeclaredRecord(TypeId id) const noexcept;

    [[nodiscard]] bool isRangeType(TypeId id) const noexcept;
    [[nodiscard]] bool isArrayType(TypeId id) const noexcept;
    [[nodiscard]] bool isTensorType(TypeId id) const noexcept;
    [[nodiscard]] bool isSparseTensorType(TypeId id) const noexcept;
    [[nodiscard]] bool isNativeTemplateType(TypeId id) const noexcept;
    [[nodiscard]] bool isNativeClassType(TypeId id) const noexcept;
    [[nodiscard]] bool isRecordType(TypeId id) const noexcept;

    [[nodiscard]] TypeId rangeElementType(TypeId id) const noexcept;
    [[nodiscard]] TypeId arrayElementType(TypeId id) const noexcept;
    [[nodiscard]] TypeId tensorElementType(TypeId id) const noexcept;
    [[nodiscard]] const std::vector<uint64_t>& arrayDimensions(TypeId id) const noexcept;
    [[nodiscard]] const std::vector<uint64_t>& tensorDimensions(TypeId id) const noexcept;

    [[nodiscard]] const std::vector<TypeId>& nativeTemplateArgs(TypeId id) const noexcept;
    /// Языковой ключ нативного шаблона: абстрактный шаблон (`templateOf`) для инстанциации, иначе
    /// сам тип (для абстрактного шаблона). Не нативный шаблон — сам `id` (canonical). R37: C++-имя
    /// в идентичность не входит.
    [[nodiscard]] TypeId nativeTemplateOwner(TypeId id) const noexcept;

    [[nodiscard]] bool isRecordTemplate(TypeId id) const noexcept;
    [[nodiscard]] const std::vector<TypeId>& templateParamsOf(TypeId id) const noexcept;
    /// Специализация record-типа (C++-модель TemplateSpecializationKind); kNone для не-специализации.
    [[nodiscard]] TemplateSpecializationKind recordSpecializationKind(TypeId id) const noexcept;

    [[nodiscard]] bool isStructType(TypeId id) const noexcept;
    [[nodiscard]] bool isClassType(TypeId id) const noexcept;
    [[nodiscard]] bool isTemplateParamType(TypeId id) const noexcept;
    [[nodiscard]] std::string_view templateParamName(TypeId id) const noexcept;
    [[nodiscard]] bool isPackExpansionType(TypeId id) const noexcept;
    [[nodiscard]] TypeId packExpansionPattern(TypeId id) const noexcept;

    [[nodiscard]] bool isAccessPolicyType(TypeId id) const noexcept;
    [[nodiscard]] bool isAccessPolicyTypeFor(RefType kind, TypeId id) const noexcept;
    [[nodiscard]] bool isDeleterPolicyType(TypeId id) const noexcept;

    /// Pointee с переносом квалификаторов входа (const/inferred) — симметрично applyRefType.
    [[nodiscard]] QualType getPointeeType(QualType state) const noexcept;

    /// Id встроенного типа по имени из общего ядра (INVALID — нет такого встроенного имени).
    [[nodiscard]] TypeId builtinIdByName(std::string_view name) const noexcept;

    /// КАНОНИЧЕСКИЙ ключ целевых C++-фактов типа (R37): сами факты живут ВНЕ языковой модели
    /// (компонент `target`); здесь — ТОЛЬКО чистый предикат адресации (без хранилища). Встроенные
    /// алиасы прозрачны (`String→StrChar`); пользовательский алиас НОМИНАЛЕН (`MyInt` — не `int32_t`);
    /// снимаются qualifier/FAST-PATH ref-биты (факты принадлежат базовому типу).
    [[nodiscard]] TypeId cppFactsKeyOf(TypeId id) const noexcept;

  private:
    /// Дескриптор типа: только для зоны `types` (E1/R49). Вне компонента читаются ТОЛЬКО именованные
    /// аксессоры (`nameOf`/`methodsOf`/`fieldsOf`/`basesOf`/…); адрес дескриптора не выдаётся.
    [[nodiscard]] const TypeDescriptor* descriptorOf(TypeId id) const noexcept;

    /// Сброс per-instance состояния к встроенному ядру (ядро только читается).
    void resetToBuiltin(const BuiltinTypeCore& core);

    /// Число встроенных дескрипторов; единственный источник — BuiltinTypeCore (0 в seed-режиме).
    [[nodiscard]] std::size_t builtinCount() const noexcept { return m_builtin != nullptr ? m_builtin->builtinCount : 0; }

    [[nodiscard]] uint32_t allocateTypeIndex() const;

    /// Добавляет пользовательский дескриптор; возвращает новый TypeId.
    TypeId push(TypeKind kind, TypeDescriptor desc);
    /// То же + проекция storage-ключа в дескриптор (namespacePath/filePrivate/module).
    TypeId push(TypeKind kind, TypeDescriptor desc, const StorageKey& key);

    /// Мутабельный дескриптор ПОЛЬЗОВАТЕЛЬСКОГО типа (builtin — nullptr: иммутабелен).
    TypeDescriptor* writableDescriptor(TypeId id) noexcept;

    void setDeclaredRecord(TypeId id, std::vector<TypeId> templateParams);
    std::optional<std::vector<TypeId>> takeDeclaredRecord(TypeId id);
    /// Изъятие пользовательских дескрипторов (при построении BuiltinTypeCore в seed-режиме).
    std::deque<TypeDescriptor> takeDescriptors();

    friend class TypeSystem;
    friend class NamedTypeRegistrar;
    friend class MethodTable;
    friend class StructuralInterner;
    friend class BuiltinCoreBuilder;
    friend class BuiltinRegistry;
    friend class TypeNameIndex;
    friend class TypePrinter;

    std::deque<TypeDescriptor> m_descriptors;
    std::map<TypeId, std::vector<TypeId>> m_declaredRecords;
    const BuiltinTypeCore* m_builtin = nullptr;
};

} // namespace trust
