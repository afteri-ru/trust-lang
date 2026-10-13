#pragma once

// include/types/call/method_signatures.hpp
// ЭФФЕКТИВНЫЕ сигнатуры набора методов для конкретного типа-объекта: подстановка T→Elem
// для Range/Array/Tensor и типовых аргументов native-template. ЕДИНЫЙ владелец этой
// трансформации для всех форм вызова (метод, `[]`, `()`, объявленные операторы, доступ к полю).

#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names/type_names.hpp"
#include "types/typekind.hpp"
#include <span>
#include <vector>

namespace trust {

/// Сигнатуры в порядке исходного набора (индексы выровнены с `MethodRef::entries` - на этом
/// держится выбор C++-суффикса по `chosenIndex`).
/// `INVALID_TYPE_ID` в позиции - инстанциация невозможна (native-template с несколькими
/// типовыми параметрами); диагностику выдаёт вызывающий (текст принадлежит форме вызова).
[[nodiscard]] inline std::vector<TypeId> effectiveMethodSignatures(TypeSystem& reg, TypeId objectType, std::span<const TypeId> signatures) {
    const TypeId canonical = reg.read().store().desugaredTypeId(objectType);
    const bool rangeLike = reg.read().store().isRangeType(objectType) || canonical == reg.read().getType(type_category::Range);
    const bool arrayLike = reg.read().store().isArrayType(objectType) || canonical == reg.read().getType(type::Array);
    const bool tensorLike =
        reg.read().store().isTensorType(objectType) || canonical == reg.read().getType(type::Tensor) || canonical == reg.read().getType(type::SparseTensor);
    const bool nativeTemplateLike = reg.read().store().isNativeTemplateType(objectType);
    std::vector<TypeId> out;
    out.reserve(signatures.size());
    for (const TypeId raw : signatures) {
        TypeId ft = raw;
        if (rangeLike) {
            ft = reg.interner().instantiateRangeMethod(objectType, ft);
        }
        if (arrayLike) {
            ft = reg.interner().instantiateArrayMethod(objectType, ft);
        }
        if (tensorLike) {
            ft = reg.interner().instantiateTensorMethod(objectType, ft);
        }
        if (nativeTemplateLike) {
            ft = reg.interner().instantiateNativeTemplateMethod(objectType, ft);
        }
        out.push_back(ft);
    }
    return out;
}

/// То же по набору метода: сигнатуры берутся из `entries` (единственный источник).
[[nodiscard]] inline std::vector<TypeId> effectiveMethodSignatures(TypeSystem& reg, TypeId objectType, const ::trust::MethodRef& method) {
    std::vector<TypeId> signatures;
    signatures.reserve(method.entries.size());
    for (const CallableSignature& entry : method.entries) {
        signatures.push_back(entry.signature);
    }
    return effectiveMethodSignatures(reg, objectType, std::span<const TypeId>(signatures));
}

} // namespace trust
