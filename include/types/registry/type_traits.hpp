#pragma once

// include/types/registry/type_traits.hpp
// Compile-time связь «альтернатива TypeData <-> TypeDataKind».
//
// Единственный источник порядка — `std::variant TypeData`: индекс альтернативы В САМОМ variant
// и есть значение `TypeDataKind` (static_assert ниже). Поэтому `isTypeDataKind` — сравнение
// индексов варианта (O(1)), а не switch по всем видам. Новый вариант `TypeData` без нового
// значения `TypeDataKind` не компилируется (static_assert на порядок/счётчик).

#include "types/type_data.hpp"

#include <cstddef>
#include <type_traits>
#include <variant>

namespace trust {

/// Число видов TypeData (= число значений TypeDataKind).
inline constexpr std::size_t kTypeDataKindCount = std::variant_size_v<TypeData>;

template <typename T, typename V>
struct TypeVariantIndex;

/// Позиция `T` в списке альтернатив; sizeof...(Ts) — не найдена (ловится static_assert).
template <typename T, typename First, typename... Rest>
constexpr std::size_t variantIndexOf(std::size_t index) {
    if constexpr (std::is_same_v<T, First>) {
        return index;
    } else {
        return variantIndexOf<T, Rest...>(index + 1);
    }
}

template <typename T>
constexpr std::size_t variantIndexOf(std::size_t index) {
    return index;
}

template <typename T, typename... Ts>
struct TypeVariantIndex<T, std::variant<Ts...>> {
    static constexpr std::size_t value = variantIndexOf<T, Ts...>(0);
    static_assert(value < sizeof...(Ts), "TypeVariantIndex: T is not an alternative of the variant");
};

/// TypeDataKind альтернативы `T`. Порядок `TypeDataKind` обязан совпадать с порядком `TypeData`.
template <typename T>
constexpr TypeDataKind typeDataKindOf() noexcept {
    return static_cast<TypeDataKind>(TypeVariantIndex<T, TypeData>::value);
}

// Инвариант «порядок TypeDataKind == порядок TypeData» без таблицы-дубликата:
// первая и последняя альтернативы обязаны попасть в первое и последнее значения enum, а число
// видов — совпасть с числом значений. Вставка/удаление альтернативы в любом месте ломает хотя бы
// одну из проверок (сдвигается индекс последней альтернативы либо счётчик).
static_assert(typeDataKindOf<SimpleTypeData>() == TypeDataKind::kSimple, "TypeDataKind is out of sync with TypeData: the first variant must map to kSimple");
static_assert(typeDataKindOf<TemplateParamTypeData>() == TypeDataKind::kTemplateParam,
              "TypeDataKind is out of sync with TypeData: the last variant must map to kTemplateParam");
static_assert(kTypeDataKindCount == static_cast<std::size_t>(TypeDataKind::kTemplateParam) + 1,
              "TypeDataKind is out of sync with TypeData: add a value for every variant (order matters)");

/// Значение TypeDataKind валидно (в пределах числа видов).
constexpr bool isValidTypeDataKind(TypeDataKind kind) noexcept {
    return static_cast<std::size_t>(kind) < kTypeDataKindCount;
}

} // namespace trust
