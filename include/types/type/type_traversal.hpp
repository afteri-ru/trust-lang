#pragma once

// include/types/type/type_traversal.hpp
// ЕДИНСТВЕННЫЙ исчерпывающий обход type-полей `TypeData` (std::visit).
//
// Единственный источник списка type-полей `TypeData`. `std::visit` делает обход исчерпывающим:
// новый вариант `TypeData` НЕ КОМПИЛИРУЕТСЯ, пока для него не описаны type-поля (compile-time
// exhaustiveness, без silent fallback). Потребитель — `collectTemplateParams`.
//
// Обходятся ТОЛЬКО поля, значение которых имеет тип TypeId (типы-дети). Не-типовые поля
// (имена, размерности, shape, cppTemplate, флаги) в обход не входят - их сравнивают
// потребители (structuralKeyTag/typeCode) по месту.

#include "types/type_system.hpp"
#include "types/type_id.hpp"

#include <type_traits>
#include <variant>

namespace trust {

namespace detail {
template <typename>
inline constexpr bool kUnhandledTypeData = false;
} // namespace detail

/// Вызвать `fn(TypeId)` для каждого type-ребёнка `data`. INVALID-поля пропускаются.
template <typename F>
void forEachTypeChild(const TypeData& data, F&& fn) {
    std::visit(
        [&](const auto& d) {
            using D = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<D, SimpleTypeData>) {
                // нет type-полей
            } else if constexpr (std::is_same_v<D, FunctionTypeData>) {
                if (d.returnType != INVALID_TYPE_ID) {
                    fn(d.returnType);
                }
                for (const TypeId p : d.paramTypes) {
                    if (p != INVALID_TYPE_ID) {
                        fn(p);
                    }
                }
                if (d.variadicType != INVALID_TYPE_ID) {
                    fn(d.variadicType);
                }
                if (d.paramPackType != INVALID_TYPE_ID) {
                    fn(d.paramPackType);
                }
            } else if constexpr (std::is_same_v<D, TemplateTypeData>) {
                if (d.templateTypeId != INVALID_TYPE_ID) {
                    fn(d.templateTypeId);
                }
                for (const TypeId a : d.args) {
                    if (a != INVALID_TYPE_ID) {
                        fn(a);
                    }
                }
            } else if constexpr (std::is_same_v<D, ArrayTypeData>) {
                if (d.elementType != INVALID_TYPE_ID) {
                    fn(d.elementType);
                }
            } else if constexpr (std::is_same_v<D, TensorTypeData>) {
                if (d.elementType != INVALID_TYPE_ID) {
                    fn(d.elementType);
                }
            } else if constexpr (std::is_same_v<D, MemberPointerTypeData>) {
                if (d.classType != INVALID_TYPE_ID) {
                    fn(d.classType);
                }
                if (d.memberType != INVALID_TYPE_ID) {
                    fn(d.memberType);
                }
            } else if constexpr (std::is_same_v<D, RefTypeData>) {
                if (d.pointeeType != INVALID_TYPE_ID) {
                    fn(d.pointeeType);
                }
                if (d.deleterType != INVALID_TYPE_ID) {
                    fn(d.deleterType);
                }
                if (d.accessPolicyType != INVALID_TYPE_ID) {
                    fn(d.accessPolicyType);
                }
                if (d.implType != INVALID_TYPE_ID) {
                    fn(d.implType);
                }
            } else if constexpr (std::is_same_v<D, PackExpansionTypeData>) {
                if (d.pattern != INVALID_TYPE_ID) {
                    fn(d.pattern);
                }
            } else if constexpr (std::is_same_v<D, TupleTypeData>) {
                for (const auto& e : d.elements) {
                    if (e.type.getTypeId() != INVALID_TYPE_ID) {
                        fn(e.type.getTypeId());
                    }
                }
            } else if constexpr (std::is_same_v<D, EnumTypeData>) {
                if (d.valueType != INVALID_TYPE_ID) {
                    fn(d.valueType);
                }
            } else if constexpr (std::is_same_v<D, VariantTypeData>) {
                for (const auto& m : d.members) {
                    if (m.type != INVALID_TYPE_ID) {
                        fn(m.type);
                    }
                }
            } else if constexpr (std::is_same_v<D, NativeTemplateTypeData>) {
                for (const TypeId a : d.args) {
                    if (a != INVALID_TYPE_ID) {
                        fn(a);
                    }
                }
            } else if constexpr (std::is_same_v<D, NativeClassTypeData>) {
                // нет type-полей (C++-имя строкой)
            } else if constexpr (std::is_same_v<D, RecordTypeData>) {
                if (d.templateOf != INVALID_TYPE_ID) {
                    // Инстанциация: типовые аргументы (поля уже подставлены).
                    for (const TypeId a : d.templateArgs) {
                        if (a != INVALID_TYPE_ID) {
                            fn(a);
                        }
                    }
                } else {
                    // Абстрактный/обычной record: поля могут содержать TemplateParam.
                    for (const auto& f : d.fields) {
                        if (f.type.getTypeId() != INVALID_TYPE_ID) {
                            fn(f.type.getTypeId());
                        }
                    }
                }
            } else if constexpr (std::is_same_v<D, TemplateParamTypeData>) {
                // Идентификатор параметра: type-полей нет (имя - не тип).
            } else {
                static_assert(detail::kUnhandledTypeData<D>, "forEachTypeChild: unhandled TypeData variant - add its type fields");
            }
        },
        data);
}

} // namespace trust
