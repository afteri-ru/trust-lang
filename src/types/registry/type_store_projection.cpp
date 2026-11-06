// Зона хранения: ПРОЕКЦИИ дескриптора — source range, record-поля/базы, namespace/module,
// routing-предикаты и identity-сравнение. Читают дескрипторы, состояния не меняют.
#include "types/registry/type_store.hpp"
#include "types/type/data_record_access.hpp"
#include "types/typekind.hpp"

#include "types/type_system.hpp" // TypeIdentityScope (единственное определение отношения)

namespace trust {

MapperRange TypeStore::getTypeSourceRange(TypeId id) const {
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        return desc->sourceRange;
    }
    return {};
}

std::string_view TypeStore::nameOf(TypeId id) const noexcept {
    const TypeDescriptor* desc = descriptorOf(id);
    return desc != nullptr ? std::string_view(desc->name) : std::string_view{};
}

const std::map<MethodKey, MethodInfo>* TypeStore::methodsOf(TypeId id) const noexcept {
    const TypeDescriptor* desc = descriptorOf(id);
    return desc != nullptr ? &desc->methods : nullptr;
}

const std::map<std::string, MethodKey>* TypeStore::methodAliasesOf(TypeId id) const noexcept {
    const TypeDescriptor* desc = descriptorOf(id);
    return desc != nullptr ? &desc->methodAliases : nullptr;
}

TypeId TypeStore::baseTypeOf(TypeId id) const noexcept {
    const TypeDescriptor* desc = descriptorOf(id);
    return desc != nullptr ? desc->baseType : INVALID_TYPE_ID;
}

const std::vector<TupleElementData>* TypeStore::fieldsOf(TypeId id) const noexcept {
    const RecordTypeData* data = getTypeDataAs<RecordTypeData>(desugaredTypeId(id));
    return data != nullptr ? &RecordTypeAccess::fieldsOf(*data) : nullptr;
}

std::optional<TypeStore::RecordInstanceView> TypeStore::instanceOf(TypeId id) const noexcept {
    const RecordTypeData* data = getTypeDataAs<RecordTypeData>(desugaredTypeId(id));
    if (data == nullptr || RecordTypeAccess::templateOf(*data) == INVALID_TYPE_ID) {
        return std::nullopt;
    }
    return RecordInstanceView{RecordTypeAccess::templateOf(*data), RecordTypeAccess::templateArgsOf(*data)};
}

const std::vector<BaseClass>& TypeStore::basesOf(TypeId id) const noexcept {
    static const std::vector<BaseClass> kEmpty;
    if (const TypeDescriptor* desc = descriptorOf(desugaredTypeId(id))) {
        return desc->baseClasses;
    }
    return kEmpty;
}

QualType TypeStore::findField(TypeId type, std::string_view name) const noexcept {
    const auto found = findFieldAccess(type, name);
    return found ? found->type : QualType{};
}

std::optional<TypeStore::FieldAccessInfo> TypeStore::findFieldAccess(TypeId type, std::string_view name) const noexcept {
    const TypeId canonical = desugaredTypeId(type);
    if (const std::vector<TupleElementData>* fields = fieldsOf(canonical)) {
        for (const auto& f : *fields) {
            if (f.name == name) {
                return FieldAccessInfo{canonical, f.type, f.access};
            }
        }
    }
    // Наследование: поле может быть унаследовано от базового класса (обход с защитой от циклов).
    for (const BaseClass& base : basesOf(canonical)) {
        const TypeId bc = desugaredTypeId(base.type);
        if (bc == canonical) {
            continue;
        }
        if (auto found = findFieldAccess(bc, name)) {
            return found;
        }
    }
    return std::nullopt;
}

std::string_view TypeStore::namespacePath(TypeId id) const {
    const TypeDescriptor* desc = descriptorOf(id);
    return desc != nullptr ? std::string_view(desc->namespacePath) : std::string_view{};
}

ModuleIndex TypeStore::moduleOf(TypeId id) const {
    const TypeDescriptor* desc = descriptorOf(id);
    return desc != nullptr ? desc->module : ModuleIndex::invalid();
}

bool TypeStore::isFilePrivate(TypeId id) const {
    const TypeDescriptor* desc = descriptorOf(id);
    return desc != nullptr && desc->filePrivate;
}

bool TypeStore::isUserDefinedType(TypeId id) const noexcept {
    // Пользовательский тип = зарегистрирован позже машинных (registry_index > builtinCount).
    // INVALID (index==0) пользовательским не считается.
    const uint32_t idx = getRegistryIndex(id);
    const uint32_t builtins = static_cast<uint32_t>(builtinCount());
    return idx != 0 && idx > builtins;
}

bool TypeStore::isBuiltinType(TypeId id) const noexcept {
    // Предварительное объявление: слот встроенного ядра. Реализация может отсутствовать.
    const uint32_t idx = getRegistryIndex(id);
    return idx != 0 && idx <= static_cast<uint32_t>(builtinCount());
}

bool TypeStore::typesEqual(TypeId a, TypeId b) const {
    if (a == b) {
        return true;
    }
    const TypeKind ka = getTypeKind(a);
    const TypeKind kb = getTypeKind(b);
    // Fast-path (признак наличия атрибутов, kHasAttrsFlag): если ни у одного типа флага нет,
    // они различны и сравнивать их достаточно по TypeId — без обращения к реестру.
    if (!hasAttrsFlag(ka) && !hasAttrsFlag(kb)) {
        return false;
    }
    // Тип несёт атрибуты (или оба): полное сравнение через реестр. Каноника берётся из ЕДИНОЙ
    // точки отношений identity (identityOf), а не через прямой вызов desugaredTypeId.
    if (identityOf(a, TypeIdentityScope::kCanonical) != identityOf(b, TypeIdentityScope::kCanonical)) {
        return false;
    }
    const TypeDescriptor* da = descriptorOf(a);
    const TypeDescriptor* db = descriptorOf(b);
    if (!da || !db) {
        return false;
    }
    if (ka != kb) {
        return false;
    }
    return da->attrs == db->attrs;
}

QualType TypeStore::getPointeeType(QualType state) const noexcept {
    return state.withIdentity(getPointeeType(state.getTypeId()));
}

} // namespace trust
