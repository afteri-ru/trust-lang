// Зона хранения: СТРУКТУРНЫЕ ВИДЫ типа (Range/Array/Tensor/native/template/pack/access/deleter).
// Единственное место, читающее дескрипторы видов; состояния не меняет.
#include "types/registry/type_store.hpp"

#include "types/group.hpp"
#include "types/names/type_names.hpp"
#include "types/type/data_record_access.hpp"
#include "types/typekind.hpp"

#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trust {

TypeId TypeStore::builtinIdByName(std::string_view name) const noexcept {
    if (m_builtin == nullptr) {
        return INVALID_TYPE_ID;
    }
    const auto it = m_builtin->name_to_id.find(exportedKeyOf(name));
    return it != m_builtin->name_to_id.end() ? it->second : INVALID_TYPE_ID;
}

bool TypeStore::isRangeType(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<TemplateTypeData>(desugaredTypeId(id));
    if (td == nullptr) {
        return false;
    }
    const TypeId range = builtinIdByName(type_category::Range);
    return range != INVALID_TYPE_ID && desugaredTypeId(td->templateTypeId) == desugaredTypeId(range);
}

TypeId TypeStore::rangeElementType(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<TemplateTypeData>(desugaredTypeId(id));
    return (td != nullptr && !td->args.empty()) ? td->args[0] : INVALID_TYPE_ID;
}

bool TypeStore::isArrayType(TypeId id) const noexcept {
    return getTypeDataAs<ArrayTypeData>(desugaredTypeId(id)) != nullptr;
}

TypeId TypeStore::arrayElementType(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<ArrayTypeData>(desugaredTypeId(id));
    return td != nullptr ? td->elementType : INVALID_TYPE_ID;
}

const std::vector<uint64_t>& TypeStore::arrayDimensions(TypeId id) const noexcept {
    static const std::vector<uint64_t> kEmpty;
    const auto* td = getTypeDataAs<ArrayTypeData>(desugaredTypeId(id));
    return td != nullptr ? td->dimensions : kEmpty;
}

bool TypeStore::isTensorType(TypeId id) const noexcept {
    return getTypeDataAs<TensorTypeData>(desugaredTypeId(id)) != nullptr;
}

bool TypeStore::isSparseTensorType(TypeId id) const noexcept {
    const TypeId canonical = desugaredTypeId(id);
    return getTypeDataAs<TensorTypeData>(canonical) != nullptr && typeGroupOf(canonical) == Group::kTensors && typeDataOf(canonical) == 2;
}

TypeId TypeStore::tensorElementType(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<TensorTypeData>(desugaredTypeId(id));
    return td != nullptr ? td->elementType : INVALID_TYPE_ID;
}

const std::vector<uint64_t>& TypeStore::tensorDimensions(TypeId id) const noexcept {
    static const std::vector<uint64_t> kEmpty;
    const auto* td = getTypeDataAs<TensorTypeData>(desugaredTypeId(id));
    return td != nullptr ? td->shape : kEmpty;
}

bool TypeStore::isNativeTemplateType(TypeId id) const noexcept {
    return typeGroupOf(desugaredTypeId(id)) == Group::kNativeTemplate;
}

const std::vector<TypeId>& TypeStore::nativeTemplateArgs(TypeId id) const noexcept {
    static const std::vector<TypeId> kEmpty;
    const auto* td = getTypeDataAs<NativeTemplateTypeData>(desugaredTypeId(id));
    return td != nullptr ? td->args : kEmpty;
}

TypeId TypeStore::nativeTemplateOwner(TypeId id) const noexcept {
    const TypeId canonical = desugaredTypeId(id);
    const auto* td = getTypeDataAs<NativeTemplateTypeData>(canonical);
    return (td != nullptr && td->templateOf != INVALID_TYPE_ID) ? desugaredTypeId(td->templateOf) : canonical;
}

bool TypeStore::isNativeClassType(TypeId id) const noexcept {
    return typeGroupOf(desugaredTypeId(id)) == Group::kNativeClass;
}

TypeId TypeStore::cppFactsKeyOf(TypeId id) const noexcept {
    // Снять qualifier-биты и FAST-PATH биты вида ссылки: C++-имя базового типа принадлежит самому
    // типу (`shared<Int32>` как бит на Int32 → факты Int32). Структурный ref-узел своих фактов не
    // несёт — его имя строит рендерер.
    TypeId base = getUnqualifiedType(id);
    if (getTypeDataAs<RefTypeData>(base) == nullptr && typeRefOf(base) != RefType::kValue) {
        base = QualType::fromTypeId(base).withKind(withRefType(getTypeKind(base), RefType::kValue)).getTypeId();
    }
    // Встроенный алиас прозрачен (String→StrChar): факты живут на каноническом типе.
    // Пользовательский алиас номинален: факты берутся по самому алиасу.
    return isBuiltinType(base) ? desugaredTypeId(base) : base;
}

bool TypeStore::isRecordType(TypeId id) const noexcept {
    const Group group = typeGroupOf(desugaredTypeId(id));
    return group == Group::kStructs || group == Group::kClassDefs;
}

bool TypeStore::isRecordTemplate(TypeId id) const noexcept {
    const auto* rd = getTypeDataAs<RecordTypeData>(desugaredTypeId(id));
    if (rd != nullptr) {
        return !RecordTypeAccess::templateParamsOf(*rd).empty();
    }
    // Объявленный, но не определённый шаблон: параметры живут в declaredRecords.
    const std::vector<TypeId>* declared = declaredRecordParams(id);
    return declared != nullptr && !declared->empty();
}

const std::vector<TypeId>& TypeStore::templateParamsOf(TypeId id) const noexcept {
    static const std::vector<TypeId> kEmpty;
    if (const auto* rd = getTypeDataAs<RecordTypeData>(desugaredTypeId(id))) {
        return RecordTypeAccess::templateParamsOf(*rd);
    }
    if (const std::vector<TypeId>* declared = declaredRecordParams(id)) {
        return *declared;
    }
    return kEmpty;
}

bool TypeStore::isStructType(TypeId id) const noexcept {
    return typeGroupOf(desugaredTypeId(id)) == Group::kStructs;
}

bool TypeStore::isClassType(TypeId id) const noexcept {
    return typeGroupOf(desugaredTypeId(id)) == Group::kClassDefs;
}

bool TypeStore::isTemplateParamType(TypeId id) const noexcept {
    return typeGroupOf(desugaredTypeId(id)) == Group::kTemplateParam;
}

std::string_view TypeStore::templateParamName(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<TemplateParamTypeData>(desugaredTypeId(id));
    return td != nullptr ? std::string_view(td->name) : std::string_view{};
}

bool TypeStore::isPackExpansionType(TypeId id) const noexcept {
    return typeGroupOf(desugaredTypeId(id)) == Group::kPackExpansion;
}

TypeId TypeStore::packExpansionPattern(TypeId id) const noexcept {
    const auto* td = getTypeDataAs<PackExpansionTypeData>(desugaredTypeId(id));
    return td != nullptr ? td->pattern : INVALID_TYPE_ID;
}

bool TypeStore::isAccessPolicyType(TypeId id) const noexcept {
    // Встроенная политика доступа: группа kAccessPolicy, Data=1..3 (политики shared/weak;
    // Data=4 - обёртка AccessShared). См. BuiltinCoreBuilder::registerBuiltinTypes.
    const TypeKind kind = getTypeKind(desugaredTypeId(id));
    const uint8_t data = typeDataOf(kind);
    return typeGroupOf(kind) == Group::kAccessPolicy && data >= 1 && data <= 3;
}

bool TypeStore::isAccessPolicyTypeFor(RefType kind, TypeId id) const noexcept {
    if (kind == RefType::kShared || kind == RefType::kWeak) {
        return isAccessPolicyType(id);
    }
    // `unique` (монопольное владение) политик доступа не имеет.
    return false;
}

bool TypeStore::isDeleterPolicyType(TypeId id) const noexcept {
    // Встроенный deleter-тип ресурса: группа kDeleterPolicy, Data>=1 (напр. trust::FreeDeleter).
    const TypeKind kind = getTypeKind(desugaredTypeId(id));
    return typeGroupOf(kind) == Group::kDeleterPolicy && typeDataOf(kind) >= 1;
}

} // namespace trust
