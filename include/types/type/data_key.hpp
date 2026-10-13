#pragma once
// include/types/type/data_key.hpp
// TypeKey и TypeKeyHash — ключ структурного интернирования типов.
// Часть модели данных типов: самостоятельным контрактом НЕ является (umbrella —
// types/type_data.hpp). Набор include повторяет umbrella, чтобы часть компилировалась
// независимо от порядка включения.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "attrs/attr.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "types/call/callable_info.hpp"
#include "types/group.hpp"
#include "types/type/ref_type.hpp"
#include "types/type_id.hpp"

namespace trust {

// -- TypeKey для структурного интернирования --------------
struct TypeKey {
    TypeKind kind;                  // TypeKind без registry_index
    std::vector<TypeId> children;   // все дочерние TypeId для сравнения
    std::vector<std::string> names; // имена полей (ТОЛЬКО для Tuple; у прочих пусто)
    // Непрозрачный для потребителей дискриминатор идентичности, не являющийся типом (не входит
    // в TypeData-поля, которые читают потребители): C++-имя нативного шаблона для инстанциации
    // (NativeTemplateTypeData::cppTemplate), layout слотов функциональной сигнатуры
    // (число параметров/variadic/pack-позиция), размерности Array и shape Tensor.
    // Без него плоский список children допускает коллизии: `pair<T>` vs `tuple<T>`,
    // `f(a,b:Any)` vs `f(a,...)`, `Array<T>` vs `Array<T,[4]>`.
    // Типизированный variant (а не строка): не-типовые поля идентичности не сериализуются
    // и не могут разойтись с проверкой равенства.
    StructuralTag tag;

    bool operator==(const TypeKey& other) const noexcept {
        return kind == other.kind && children == other.children && names == other.names && tag == other.tag;
    }
};

struct TypeKeyHash {
    size_t operator()(const TypeKey& key) const noexcept {
        size_t h = std::hash<uint32_t>{}(static_cast<uint32_t>(key.kind));
        for (auto child : key.children) {
            h ^= std::hash<TypeId>{}(child) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        for (const auto& n : key.names) {
            h ^= std::hash<std::string>{}(n) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        const size_t tagHash = std::visit(
            [](const auto& t) -> size_t {
                using T = std::decay_t<decltype(t)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return 0;
                } else if constexpr (std::is_same_v<T, SlotLayoutTag>) {
                    const uint64_t packed = (static_cast<uint64_t>(t.paramCount) << 9) | (static_cast<uint64_t>(t.hasVariadic) << 8) | t.paramPackPosition;
                    return std::hash<uint64_t>{}(packed);
                } else if constexpr (std::is_same_v<T, NativeTemplateTag>) {
                    return std::hash<std::string>{}(t.cppTemplate);
                } else if constexpr (std::is_same_v<T, ArrayShapeTag>) {
                    size_t shapeHash = std::hash<bool>{}(t.isVariadic);
                    for (uint64_t d : t.dimensions) {
                        shapeHash ^= std::hash<uint64_t>{}(d) + 0x9e3779b9 + (shapeHash << 6) + (shapeHash >> 2);
                    }
                    return shapeHash;
                } else if constexpr (std::is_same_v<T, TensorShapeTag>) {
                    size_t shapeHash = 0;
                    for (uint64_t d : t.shape) {
                        shapeHash ^= std::hash<uint64_t>{}(d) + 0x9e3779b9 + (shapeHash << 6) + (shapeHash >> 2);
                    }
                    return shapeHash;
                } else if constexpr (std::is_same_v<T, TemplateMarkerTag>) {
                    return std::hash<TypeId>{}(t.templateTypeId);
                } else if constexpr (std::is_same_v<T, TemplateParamTag>) {
                    size_t h = std::hash<TypeId>{}(t.owner);
                    h ^= std::hash<uint32_t>{}(t.depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    h ^= std::hash<uint32_t>{}(t.index) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    return h;
                } else {
                    static_assert(!std::is_same_v<T, T>, "TypeKeyHash: unhandled StructuralTag alternative - add its hash");
                    return 0;
                }
            },
            key.tag);
        h ^= tagHash + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace trust
