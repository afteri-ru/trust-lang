// src/types/registry/named_type_registrar.cpp
// Зона создания именованных типов: транзакция reserve -> push -> bind.
#include "types/registry/named_type_registrar.hpp"

#include "diag/base_diags.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/report.hpp"
#include "types/group.hpp"
#include "types/type_system.hpp"
#include "utils/error.hpp"

namespace trust {

namespace {

/// Группа record-типа как человекочитаемый вид (для диагностик объявления/определения).
constexpr std::string_view recordGroupName(Group g) noexcept {
    return (g == Group::kStructs) ? "Struct" : "Class";
}

} // namespace

TypeId NamedTypeRegistrar::registerType(std::string_view name, TypeId baseTypeId, std::vector<AttrId> attrs, MapperRange sourceRange,
                                        std::string_view preprocInclude, bool hasTrust, std::optional<StorageKey> storageKey) {
    EXPECT(isUnqualifiedType(baseTypeId) && "registerType: base type id must not carry type-state flags");
    TypeKind kind = getTypeKind(baseTypeId);
    const StorageKey key = m_names.registrationKey(storageKey, name);
    if (!m_names.reserve(key, name, sourceRange)) {
        return INVALID_TYPE_ID;
    }

    // BuiltinFlag - признак ПРЕДОБЪЯВЛЕННОГО типа: пользовательский алиас его НЕ наследует.
    // Встроенные алиасы регистрируются ТОЛЬКО через registerBuiltinAlias.
    kind = clearBuiltinFlag(kind);
    if (hasTrust) {
        kind = setTrustFlag(kind); // trust-условия - семантический дифференциатор идентичности алиаса
    }
    if (!attrs.empty()) {
        kind = setAttrsFlag(kind); // признак «тип несёт атрибуты» (kHasAttrsFlag) -> сравнение через реестр
    }
    // registry_index для пользовательского типа = после всех встроенных.
    return registerAlias(kind, name, baseTypeId, std::move(attrs), sourceRange, preprocInclude, key, false);
}

TypeId NamedTypeRegistrar::registerBuiltinAlias(std::string_view name, TypeId target) {
    EXPECT(target != INVALID_TYPE_ID && "registerBuiltinAlias: target type id is invalid");
    EXPECT(!m_store.builtinCore() && "registerBuiltinAlias: only valid while building builtin core");
    const StorageKey key = m_names.registrationKey(std::nullopt, name);
    if (!m_names.reserve(key, name, MapperRange{})) {
        return INVALID_TYPE_ID;
    }
    TypeKind kind = setBuiltinFlag(getTypeKind(target));
    return registerAlias(kind, name, target, {}, {}, {}, key, true);
}

TypeId NamedTypeRegistrar::registerAlias(TypeKind kind, std::string_view name, TypeId baseTypeId, std::vector<AttrId> attrs, MapperRange sourceRange,
                                         std::string_view preprocInclude, const StorageKey& key, bool processPool) {
    TypeDescriptor desc{
        std::string(name),                                                                                           // name - владеющая копия
        std::move(attrs),                                                                                            // attrs
        sourceRange,                                                                                                 // sourceRange
        {},                                                                                                          // cppName (empty for aliases)
        preprocInclude.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::string(preprocInclude)}, // preprocIncludes
        baseTypeId,                                                                                                  // baseType - points to the aliased type
        SimpleTypeData{}                                                                                             // data - aliases are simple types
    };
    desc.canonicalType = m_store.desugaredTypeId(baseTypeId);
    const TypeId id = m_store.push(kind, std::move(desc), key);
    m_names.bind(key, id, processPool ? TypeNameIndex::KeyPool::kProcess : TypeNameIndex::KeyPool::kCompilation);
    return id;
}

TypeId NamedTypeRegistrar::registerEnumType(std::string_view name, TypeId valueType, std::vector<EnumMemberData> members, MapperRange sourceRange,
                                            bool hasTrust, std::optional<StorageKey> storageKey) {
    const StorageKey key = m_names.registrationKey(storageKey, name);
    if (!m_names.reserve(key, name, sourceRange)) {
        return INVALID_TYPE_ID;
    }

    // Group::kEnums, data=1 - конкретный enum-тип (аналог kStructured для кортежа).
    EXPECT(isUnqualifiedType(valueType) && "registerEnumType: value type id must not carry type-state flags");
    TypeKind kind = makeTypeKind(Group::kEnums, 1);
    if (hasTrust) {
        kind = setTrustFlag(kind);
    }
    EnumTypeData data{valueType, std::move(members)};
    TypeDescriptor desc{
        std::string(name), // name - владеющая копия
        {},                // attrs (пусто - атрибуты enum пока не используются)
        sourceRange,       // sourceRange - позиция объявления enum
        {},                // cppName (пусто - манглинг в кодогенерации: nameToCpp)
        {},                // preprocIncludes (enum-структура самодостаточна; инклуды тянут типы значений)
        INVALID_TYPE_ID,   // baseType - enum НЕ алиас (canonical = сам тип)
        std::move(data)    // data - EnumTypeData
    };
    const TypeId id = m_store.push(kind, std::move(desc), key);
    m_names.bind(key, id);
    return id;
}

TypeId NamedTypeRegistrar::registerVariantType(std::string_view name, std::vector<VariantMemberData> members, MapperRange sourceRange, bool hasTrust,
                                               std::optional<StorageKey> storageKey) {
    const StorageKey key = m_names.registrationKey(storageKey, name);
    if (!m_names.reserve(key, name, sourceRange)) {
        return INVALID_TYPE_ID;
    }

    // Group::kVariants, data=1 - конкретный вариант-тип.
    for (const auto& member : members) {
        EXPECT(isUnqualifiedType(member.type) && "registerVariantType: member type id must not carry type-state flags");
    }
    TypeKind kind = makeTypeKind(Group::kVariants, 1);
    if (hasTrust) {
        kind = setTrustFlag(kind);
    }
    VariantTypeData data{std::move(members)};
    TypeDescriptor desc{
        std::string(name), // name - владеющая копия
        {},                // attrs
        sourceRange,       // sourceRange
        {},                // cppName (манглинг в кодогенерации: nameToCpp)
        {},                // preprocIncludes (заголовки тянут типы членов; <variant> - кодогенерация)
        INVALID_TYPE_ID,   // baseType - Variant НЕ алиас
        std::move(data)    // data - VariantTypeData
    };
    const TypeId id = m_store.push(kind, std::move(desc), key);
    m_names.bind(key, id);
    return id;
}

TypeId NamedTypeRegistrar::declareRecord(std::string_view name, Group group, bool isTemplate, MapperRange sourceRange, bool hasTrust,
                                         std::optional<StorageKey> storageKey) {
    EXPECT((group == Group::kStructs || group == Group::kClassDefs) && "declareRecord: group must be kStructs or kClassDefs");
    const StorageKey key = m_names.registrationKey(storageKey, name);
    // Имя уже занято пользовательским типом?
    if (m_names.contains(key)) {
        const TypeId existing = *m_names.find(key);
        if (m_store.isRecordType(existing)) {
            const MapperRange prevRange = m_store.getTypeSourceRange(existing);
            const bool sameFile = !prevRange.isInvalid() && !sourceRange.isInvalid() && prevRange.begin.fileIdx() == sourceRange.begin.fileIdx();
            // Идемпотентность ТОЛЬКО для одного модуля: продолжение forward→definition в том же
            // файле либо повторная регистрация того же узла. Другой файл = другой модуль ⇒ дубль.
            if (sameFile && (m_store.hasDeclaredRecord(existing) || prevRange == sourceRange)) {
                return existing;
            }
            m_names.reportDuplicate(key, name, sourceRange);
            return INVALID_TYPE_ID;
        }
        // Имя занято НЕ-record типом (алиас/enum/...): дубликат.
        m_names.reportDuplicate(key, name, sourceRange);
        return INVALID_TYPE_ID;
    }
    if (!m_names.reserve(key, name, sourceRange)) {
        return INVALID_TYPE_ID;
    }
    // Record-тип НЕ алиас (baseType=INVALID → canonical = сам тип). Шаблон - Data=0; обычный
    // record - Data=1. `data == nullopt` = объявлен, но не определён (defineRecord заполнит);
    // типовые параметры объявленного шаблона хранятся в TypeStore::declaredRecords (нужны для
    // self-инстанциации `Box<T>` внутри собственного тела ДО определения) и задаются ПОЗЖЕ
    // (`setRecordTemplateParams`), т.к. их владельцем должен быть сам TypeId записи.
    TypeKind kind = makeTypeKind(group, isTemplate ? 0 : 1);
    if (hasTrust) {
        kind = setTrustFlag(kind);
    }
    TypeDescriptor desc{
        std::string(name), // name - владеющая копия
        {},                // attrs
        sourceRange,       // sourceRange - позиция объявления
        {},                // cppName (манглинг в кодогенерации: nameToCpp)
        {},                // preprocIncludes (struct самодостаточен; инклуды тянут типы полей/баз)
        INVALID_TYPE_ID,   // baseType - record НЕ алиас
        std::nullopt,      // data - объявлен, но НЕ определён (заполнит defineRecord)
        {},                // methods
        {},                // methodAliases
        {}                 // baseClasses (заполнит defineRecord)
    };
    const TypeId id = m_store.push(kind, std::move(desc), key);
    m_store.setDeclaredRecord(id, {}); // параметры шаблона задаются setRecordTemplateParams (owner = id)
    m_names.bind(key, id);
    return id;
}

void NamedTypeRegistrar::setRecordTemplateParams(TypeId id, std::vector<TypeId> params) {
    for (const TypeId p : params) {
        EXPECT(isUnqualifiedType(p) && "setRecordTemplateParams: template param id must not carry type-state flags");
    }
    m_store.setDeclaredRecord(id, std::move(params));
}

TypeId NamedTypeRegistrar::defineRecord(std::string_view name, Group group, std::vector<TupleElementData> fields, std::vector<TypeId> baseClasses,
                                        MapperRange sourceRange, bool hasTrust, std::optional<StorageKey> storageKey) {
    EXPECT((group == Group::kStructs || group == Group::kClassDefs) && "defineRecord: group must be kStructs or kClassDefs");
    for (const auto& field : fields) {
        EXPECT(isUnqualifiedType(field.type.getTypeId()) && "defineRecord: field type identity must not carry type-state flags");
    }
    for (const TypeId base : baseClasses) {
        EXPECT(isUnqualifiedType(base) && "defineRecord: base class id must not carry type-state flags");
    }
    const StorageKey key = m_names.registrationKey(storageKey, name);
    if (!m_names.contains(key)) {
        // Не объявлен заранее: создаём сразу полностью определённый record (обычный путь).
        if (!m_names.reserve(key, name, sourceRange)) {
            return INVALID_TYPE_ID;
        }
        TypeKind kind = makeTypeKind(group, 1);
        if (hasTrust) {
            kind = setTrustFlag(kind);
        }
        RecordTypeData data{std::move(fields)};
        TypeDescriptor desc{std::string(name), {}, sourceRange, {}, {}, INVALID_TYPE_ID, std::move(data), {}, {}, std::move(baseClasses)};
        const TypeId newId = m_store.push(kind, std::move(desc), key);
        m_names.bind(key, newId);
        return newId;
    }
    const TypeId id = *m_names.find(key);
    if (!m_store.isRecordType(id)) {
        diag::reportWithOptions(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "type name '{}' is already used by a non-record type",
                                name);
        return INVALID_TYPE_ID;
    }
    if (getGroup(getTypeKind(id)) != group) {
        diag::reportWithOptions(m_diag, m_opts, diag::DiagId::ParseError, sourceRange,
                                "record '{}' was declared as {} and cannot be defined as {}", name, recordGroupName(getGroup(getTypeKind(id))),
                                recordGroupName(group));
        return INVALID_TYPE_ID;
    }
    // Маркер объявления читается копией ДО записи: снимается ТОЛЬКО после успешного заполнения
    // data/baseClasses, иначе неудачное определение потеряло бы объявление (полуприменённое состояние).
    const std::vector<TypeId>* declaredParams = m_store.declaredRecordParams(id);
    if (declaredParams == nullptr) {
        // Record уже определён (или имя занято - выше) - повторное определение.
        MapperRange prevRange = m_store.getTypeSourceRange(id);
        if (!prevRange.isInvalid()) {
            diag::reportWithOptions(m_diag, m_opts, diag::DiagId::ParseError, prevRange, "previous definition of type '{}'", name);
        }
        diag::reportWithOptions(m_diag, m_opts, diag::DiagId::ParseError, sourceRange, "duplicate type name '{}'", name);
        return INVALID_TYPE_ID;
    }
    std::vector<TypeId> templateParams = *declaredParams;
    TypeDescriptor* desc = m_store.writableDescriptor(id);
    EXPECT(desc != nullptr && "defineRecord: declared record must be a user type");
    RecordTypeData data{std::move(fields)};
    data.templateParams = std::move(templateParams);
    desc->data = TypeData{std::move(data)};
    desc->baseClasses = std::move(baseClasses);
    const std::optional<std::vector<TypeId>> takenMarker = m_store.takeDeclaredRecord(id);
    EXPECT(takenMarker.has_value() && "defineRecord: declared record marker disappeared");
    return id;
}

TypeId NamedTypeRegistrar::registerNativeTemplate(std::string_view name, std::string_view cppTemplate, MapperRange sourceRange, std::string_view preprocInclude,
                                                  std::vector<std::string> libs, std::optional<StorageKey> storageKey) {
    const StorageKey key = m_names.registrationKey(storageKey, name);
    if (!m_names.reserve(key, name, sourceRange)) {
        return INVALID_TYPE_ID;
    }
    // Абстрактный шаблон: Group::kNativeTemplate, Data=0, name = trust-имя. NativeTemplateTypeData
    // с пустым args - маркер шаблона (cppTemplate хранит C++-имя). children пуст.
    TypeKind kind = makeTypeKind(Group::kNativeTemplate, 0);
    NativeTemplateTypeData data{std::string(cppTemplate), {}};
    TypeDescriptor desc{
        std::string(name), // name - владеющая копия
        {},                // attrs
        sourceRange,       // sourceRange
        {},                // cppName (манглинг в кодогенерации; C++-имя - в NativeTemplateTypeData)
        preprocInclude.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::string(preprocInclude)},
        INVALID_TYPE_ID, // baseType - не алиас
        std::move(data), // data - NativeTemplateTypeData
        {},              // methods
        {},              // methodAliases
        {},              // baseClasses
        std::move(libs)  // libs - @[link(...)] on-use
    };
    const TypeId id = m_store.push(kind, std::move(desc), key);
    m_names.bind(key, id);
    return id;
}

TypeId NamedTypeRegistrar::registerNativeClass(std::string_view name, std::string_view cppName, MapperRange sourceRange, std::string_view preprocInclude,
                                               std::vector<std::string> libs, std::optional<StorageKey> storageKey) {
    const StorageKey key = m_names.registrationKey(storageKey, name);
    if (!m_names.reserve(key, name, sourceRange)) {
        return INVALID_TYPE_ID;
    }
    // Нативный класс: Group::kNativeClass, Data=0, name = trust-имя, C++-имя - в
    // NativeClassTypeData{cppName}. C++-struct НЕ генерируется (класс определён в заголовке).
    TypeKind kind = makeTypeKind(Group::kNativeClass, 0);
    NativeClassTypeData data{std::string(cppName)};
    TypeDescriptor desc{
        std::string(name), // name - владеющая копия
        {},                // attrs
        sourceRange,       // sourceRange
        {},                // cppName (манглинг НЕ используется; C++-имя - в NativeClassTypeData)
        preprocInclude.empty() ? std::vector<std::string>{} : std::vector<std::string>{std::string(preprocInclude)},
        INVALID_TYPE_ID, // baseType - не алиас
        std::move(data), // data - NativeClassTypeData
        {},              // methods
        {},              // methodAliases
        {},              // baseClasses
        std::move(libs)  // libs - @[link(...)] on-use
    };
    const TypeId id = m_store.push(kind, std::move(desc), key);
    m_names.bind(key, id);
    return id;
}

} // namespace trust
