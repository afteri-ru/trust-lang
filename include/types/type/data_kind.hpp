#pragma once
// TypeData-variant, TypeDataKind и типизированные дискриминаторы идентичности (StructuralTag).
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

using TypeData =
    std::variant<SimpleTypeData, FunctionTypeData, TemplateTypeData, ArrayTypeData, TensorTypeData, MemberPointerTypeData, RefTypeData, PackExpansionTypeData,
                 TupleTypeData, EnumTypeData, VariantTypeData, NativeTemplateTypeData, NativeClassTypeData, RecordTypeData, TemplateParamTypeData>;

enum class TypeDataKind : uint8_t {
    kSimple,
    kFunction,
    kTemplate,
    kArray,
    kTensor,
    kMemberPointer,
    kRefType,
    kPackExpansion,
    kTuple,
    kEnum,
    kVariant,
    kNativeTemplate,
    kNativeClass,
    kRecord,
    kTemplateParam,
};

/// Slot layout of a function signature: parameter count, C-variadic slot, pack position.
/// Return type and the trust bit are identity parts OUTSIDE the layout (see FunctionTypeData).
struct SlotLayoutTag {
    uint32_t paramCount{0};
    bool hasVariadic{false};
    uint8_t paramPackPosition{0};

    friend bool operator==(const SlotLayoutTag&, const SlotLayoutTag&) = default;
};

/// Language key of a native-template instantiation: the abstract template it instantiates
/// (`std::pair<T>` != `std::tuple<T>` by their abstract template, NOT by a C++ name — the C++ name
/// is a target fact, not part of language identity: R37, §4 ARCHITECTURE.md).
struct NativeTemplateTag {
    TypeId templateOf{INVALID_TYPE_ID};

    friend bool operator==(const NativeTemplateTag&, const NativeTemplateTag&) = default;
};

/// Fixed/open array shape: dimensions + slice marker. `ArrayTypeData` fields are NOT TypeIds, so
/// they cannot travel in `TypeKey::children`; they are part of identity and live in this typed tag
/// (a string encoding in `TypeKeyChild::name` would be a second, serialized representation).
struct ArrayShapeTag {
    std::vector<uint64_t> dimensions;
    bool isVariadic{false};

    friend bool operator==(const ArrayShapeTag&, const ArrayShapeTag&) = default;
};

/// Static tensor shape (`empty` = dynamic/unknown). Same reasoning as `ArrayShapeTag`.
struct TensorShapeTag {
    std::vector<uint64_t> shape;

    friend bool operator==(const TensorShapeTag&, const TensorShapeTag&) = default;
};

/// Template marker of a parameterized type (`Range<Elem>` vs another template with the same args).
struct TemplateMarkerTag {
    TypeId templateTypeId{INVALID_TYPE_ID};

    friend bool operator==(const TemplateMarkerTag&, const TemplateMarkerTag&) = default;
};

/// Identity типового параметра: владелец-шаблон + позиция (depth, index) — C++-модель
/// TemplateTypeParmDecl. Уникальность даёт ПРИНАДЛЕЖНОСТЬ параметра своему шаблону (`owner`), а не
/// только имя: `A<T>` и `B<T>` имеют разные `owner`, поэтому их `T` — разные TypeId. Имя — только
/// для диагностик; для не-scoped параметров (owner=INVALID, generic-функции) продолжает жить в
/// TypeKeyChild::name, поэтому интернирование по имени сохраняется.
struct TemplateParamTag {
    TypeId owner{INVALID_TYPE_ID};
    uint32_t depth{0};
    uint32_t index{0};

    friend bool operator==(const TemplateParamTag&, const TemplateParamTag&) = default;
};

/// Typed identity discriminator DERIVED from TypeData (never a caller responsibility).
/// `std::monostate` - identity is fully expressed by kind/children.
using StructuralTag = std::variant<std::monostate, SlotLayoutTag, NativeTemplateTag, ArrayShapeTag, TensorShapeTag, TemplateMarkerTag, TemplateParamTag>;

} // namespace trust
