// src/types/registry/type_store_projection.cpp
// Зона хранения: ПРОЕКЦИИ дескриптора — source range, record-поля/базы, namespace/module,
// routing-предикаты и identity-сравнение. Читают дескрипторы, состояния не меняют.
#include "types/registry/type_store.hpp"
#include "types/typekind.hpp"

#include "types/type_system.hpp" // TypeIdentityScope (единственное определение отношения)

namespace trust {

MapperRange TypeStore::getTypeSourceRange(TypeId id) const {
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        return desc->sourceRange;
    }
    return {};
}

const RecordTypeData* TypeStore::recordData(TypeId id) const noexcept {
    return getTypeDataAs<RecordTypeData>(desugaredTypeId(id));
}

const std::vector<TypeId>& TypeStore::baseClasses(TypeId id) const noexcept {
    static const std::vector<TypeId> kEmpty;
    if (const TypeDescriptor* desc = descriptorOf(desugaredTypeId(id))) {
        return desc->baseClasses;
    }
    return kEmpty;
}

QualType TypeStore::findField(TypeId type, std::string_view name) const noexcept {
    const TypeId canonical = desugaredTypeId(type);
    if (const auto* rd = getTypeDataAs<RecordTypeData>(canonical)) {
        for (const auto& f : rd->fields) {
            if (f.name == name) {
                return f.type;
            }
        }
    }
    // Наследование: поле может быть унаследовано от базового класса (обход с защитой от циклов).
    for (const TypeId base : baseClasses(canonical)) {
        const TypeId bc = desugaredTypeId(base);
        if (bc == canonical) {
            continue;
        }
        if (const QualType ft = findField(bc, name); ft.isValid()) {
            return ft;
        }
    }
    return QualType{};
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
