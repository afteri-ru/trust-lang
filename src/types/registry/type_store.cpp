// src/types/registry/type_store.cpp
// Зона хранения типов: дескрипторы, registry_index, проекции identity/поля/namespace.
#include "types/registry/type_store.hpp"

#include "types/names/storage_key.hpp"
#include "types/registry/type_traits.hpp"
#include "utils/error.hpp"


namespace trust {

const TypeDescriptor* TypeStore::descriptorOf(TypeId id) const noexcept {
    const uint32_t idx = getRegistryIndex(id);
    if (idx == 0) {
        return nullptr;
    }
    const std::size_t builtins = builtinCount();
    if (idx <= builtins) {
        return &m_builtin->descriptors[idx - 1];
    }
    const std::size_t u = static_cast<std::size_t>(idx) - 1 - builtins;
    return (u < m_descriptors.size()) ? &m_descriptors[u] : nullptr;
}

TypeDescriptor* TypeStore::writableDescriptor(TypeId id) noexcept {
    const uint32_t idx = getRegistryIndex(id);
    const std::size_t builtins = builtinCount();
    if (idx == 0 || idx <= builtins) {
        return nullptr; // встроенный — иммутабелен
    }
    const std::size_t u = static_cast<std::size_t>(idx) - 1 - builtins;
    return (u < m_descriptors.size()) ? &m_descriptors[u] : nullptr;
}

uint32_t TypeStore::allocateTypeIndex() const {
    const std::size_t next = builtinCount() + m_descriptors.size() + 1;
    EXPECT(next <= kMaxTypeIndex && "TypeStore: registry_index overflow (see kTypeIndexBits)");
    return static_cast<uint32_t>(next);
}

TypeId TypeStore::push(TypeKind kind, TypeDescriptor desc) {
    const TypeId id = makeTypeId(kind, allocateTypeIndex());
    m_descriptors.push_back(std::move(desc));
    return id;
}

TypeId TypeStore::push(TypeKind kind, TypeDescriptor desc, const StorageKey& key) {
    const TypeId id = makeTypeId(kind, allocateTypeIndex());
    m_descriptors.push_back(std::move(desc));
    applyTypeStorageProjection(m_descriptors.back(), key);
    return id;
}

const std::optional<TypeData>& TypeStore::getTypeData(TypeId id) const noexcept {
    static const std::optional<TypeData> kEmpty;
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        return desc->data;
    }
    return kEmpty;
}

bool TypeStore::isTypeDataKind(TypeId id, TypeDataKind kind) const noexcept {
    ASSERT(isValidTypeDataKind(kind) && "isTypeDataKind: unknown TypeDataKind value");
    const auto& data = getTypeData(id);
    // O(1): значение TypeDataKind — индекс альтернативы в std::variant TypeData
    // (инвариант закреплён static_assert в types/registry/type_traits.hpp).
    return data && data->index() == static_cast<std::size_t>(kind);
}

bool TypeStore::isCompleteType(TypeId id) const noexcept {
    return getTypeData(id).has_value();
}

bool TypeStore::isForwardDecl(TypeId id) const noexcept {
    return !getTypeData(id).has_value();
}

TypeId TypeStore::semanticTypeId(TypeId id) const noexcept {
    const TypeId s = getUnqualifiedType(id);
    // Встроенные типы и встроенные алиасы — representation: идентичность == реализация
    // (Integer -> Int64, String -> StrChar, ...). Пользовательские алиасы номинальны.
    return isBuiltinType(s) ? desugaredTypeId(s) : s;
}

TypeId TypeStore::desugaredTypeId(TypeId id) const noexcept {
    // Структурная идентичность: снимаем ВСЕ поведенческие флаги (kQualifierMask) через единый
    // слой (getUnqualifiedType) — они не часть ключа интернирования.
    id = getUnqualifiedType(id);
    const TypeDescriptor* desc = descriptorOf(id);
    if (desc != nullptr && desc->baseType != INVALID_TYPE_ID) {
        // Инвариант registerType: алиас всегда имеет заполненный canonicalType (корень baseType-цепочки).
        ASSERT(desc->canonicalType != INVALID_TYPE_ID);
        return desc->canonicalType;
    }
    return id; // non-alias: preserve the exact kind (fast-path RefType bits included)
}

TypeId TypeStore::identityOf(TypeId id, TypeIdentityScope scope) const {
    switch (scope) {
    case TypeIdentityScope::kStructural:
        return getUnqualifiedType(id);
    case TypeIdentityScope::kSemantic:
        return semanticTypeId(id);
    case TypeIdentityScope::kCanonical:
        return desugaredTypeId(id);
    }
    FAULT("identityOf: unknown TypeIdentityScope value {}", static_cast<int>(scope));
}

TypeId TypeStore::getBaseType(TypeId id) const noexcept {
    if (const TypeDescriptor* desc = descriptorOf(id)) {
        return desc->baseType;
    }
    return INVALID_TYPE_ID;
}

TypeId TypeStore::getPointeeType(TypeId id) const noexcept {
    const TypeId structural = getUnqualifiedType(id);
    // Структурный ссылочный узел (ссылку на уже ссылочный тип): pointee — ребёнок узла.
    if (const auto* rd = getTypeDataAs<RefTypeData>(structural)) {
        return rd->pointeeType;
    }
    // Fast-path бит ссылки: pointee — тот же identity-тип со снятыми битами RefType.
    if (getRefType(getTypeKind(structural)) != RefType::kValue) {
        const TypeKind kind = withRefType(getTypeKind(structural), RefType::kValue);
        return QualType::fromTypeId(structural).withKind(kind).getTypeId();
    }
    return structural;
}

const std::vector<TypeId>* TypeStore::declaredRecordParams(TypeId id) const noexcept {
    const auto it = m_declaredRecords.find(id);
    return it == m_declaredRecords.end() ? nullptr : &it->second;
}

bool TypeStore::hasDeclaredRecord(TypeId id) const noexcept {
    return m_declaredRecords.find(id) != m_declaredRecords.end();
}

void TypeStore::setDeclaredRecord(TypeId id, std::vector<TypeId> templateParams) {
    // insert-or-assign: declareRecord создаёт пустой маркер, setRecordTemplateParams заменяет параметры.
    m_declaredRecords[id] = std::move(templateParams);
}

std::optional<std::vector<TypeId>> TypeStore::takeDeclaredRecord(TypeId id) {
    auto it = m_declaredRecords.find(id);
    if (it == m_declaredRecords.end()) {
        return std::nullopt;
    }
    std::vector<TypeId> params = std::move(it->second);
    m_declaredRecords.erase(it);
    return params;
}

std::deque<TypeDescriptor> TypeStore::takeDescriptors() {
    std::deque<TypeDescriptor> out = std::move(m_descriptors);
    m_descriptors.clear();
    return out;
}

TemplateSpecializationKind TypeStore::recordSpecializationKind(TypeId id) const noexcept {
    const TypeDescriptor* desc = descriptorOf(id);
    return desc != nullptr ? desc->specializationKind : TemplateSpecializationKind::kNone;
}

void TypeStore::resetToBuiltin(const BuiltinTypeCore& core) {
    m_builtin = &core;
    m_descriptors.clear();
    m_declaredRecords.clear();
}

} // namespace trust
