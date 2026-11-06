#pragma once

// Зона «создание и жизненный цикл ИМЕНОВАННЫХ типов»: алиас (`::=`), второе имя (`::-`),
// enum/variant, record (forward -> definition), native template/class.
// Транзакция: reserve имени (TypeNameIndex) -> push дескриптора (TypeStore) -> bind ключа
// (TypeNameIndex). Примитивы зон приватны; NamedTypeRegistrar — их единственный друг вместе с
// StructuralInterner. Публичный API — только методы создания; чтение остаётся у TypeSystem.

#include "attrs/attr.hpp"
#include "location/location.hpp"
#include "types/names/storage_key.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace trust {

class DiagnosticEngine;
class Options;
class TypeNameIndex;
class TypeStore;

class NamedTypeRegistrar {
  public:
    NamedTypeRegistrar(TypeStore& store, TypeNameIndex& names, DiagnosticEngine& diag, const Options& opts) noexcept
    : m_store(store)
    , m_names(names)
    , m_diag(diag)
    , m_opts(opts) {}

    /// Пользовательский алиас на существующий тип (`Name ::= Base`).
    TypeId registerType(std::string_view name, TypeId baseTypeId, std::vector<AttrId> attrs = {}, MapperRange sourceRange = {}, bool hasTrust = false,
                        std::optional<StorageKey> storageKey = std::nullopt);

    /// Встроенный алиас (seed-ядро): ставит BuiltinFlag и регистрирует имя в процессном пуле.
    /// ЕДИНСТВЕННЫЙ путь установки BuiltinFlag (registerType всегда его сбрасывает).
    TypeId registerBuiltinAlias(std::string_view name, TypeId target);

    TypeId registerEnumType(std::string_view name, TypeId valueType, std::vector<EnumMemberData> members, MapperRange sourceRange = {}, bool hasTrust = false,
                            std::optional<StorageKey> storageKey = std::nullopt);
    TypeId registerVariantType(std::string_view name, std::vector<VariantMemberData> members, MapperRange sourceRange = {}, bool hasTrust = false,
                               std::optional<StorageKey> storageKey = std::nullopt);

    /// Forward-объявление record-типа (data == nullopt до `defineRecord`).
    /// `isTemplate` — признак шаблон-класса (Group/Data=0). Типовые параметры задаются ПОСЛЕ
    /// (`setRecordTemplateParams`): их владелец = возвращаемый TypeId записи (owner-scoping,
    /// C++-модель TemplateTypeParmDecl — одинаковые имена в разных шаблонах не конфликтуют).
    TypeId declareRecord(std::string_view name, Group group, bool isTemplate = false, MapperRange sourceRange = {}, bool hasTrust = false,
                         std::optional<StorageKey> storageKey = std::nullopt);

    /// Типовые параметры объявленного шаблон-класса (установка после создания именованных параметров
    /// с owner = TypeId записи). Заменяет параметры в forward-маркере объявления (`m_declaredRecords`).
    void setRecordTemplateParams(TypeId id, std::vector<TypeId> params);

    /// Определение record-типа (завершает forward в том же файле; иначе дубликат).
    TypeId defineRecord(std::string_view name, Group group, std::vector<TupleElementData> fields, std::vector<BaseClass> baseClasses = {},
                        MapperRange sourceRange = {}, bool hasTrust = false, std::optional<StorageKey> storageKey = std::nullopt);

    TypeId registerNativeTemplate(std::string_view name, MapperRange sourceRange, std::optional<StorageKey> storageKey = std::nullopt);
    TypeId registerNativeClass(std::string_view name, MapperRange sourceRange, std::optional<StorageKey> storageKey = std::nullopt);

  private:
    /// Единая транзакция алиаса: reserve выполнен вызывающим, key уже зарезервирован.
    TypeId registerAlias(TypeKind kind, std::string_view name, TypeId baseTypeId, std::vector<AttrId> attrs, MapperRange sourceRange, const StorageKey& key,
                         bool processPool);

    TypeStore& m_store;
    TypeNameIndex& m_names;
    DiagnosticEngine& m_diag;
    const Options& m_opts;
};

} // namespace trust
