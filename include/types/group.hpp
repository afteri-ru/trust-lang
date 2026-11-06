#pragma once

#include <cstdint>
#include <string_view>

#include "types/typekind.hpp"

namespace trust {

// `Group` объявлен в общем ABI-заголовке `runtime/trust/type_state.hpp` (рядом с TypeKind),
// чтобы компилятор и рантайм использовали ОДНИ числовые коды. Здесь - только Category и
// таблица принадлежности.
enum class Category : uint8_t {
    kVoid,
    kAny,
    kArithmetics,
    kStrings,
    kTensors,
    kContainers,
    kStructured,
    kCallable,
    kClasses,
    kRanges,
    kIterators,
    kDateTime,
    kAsync,
    kSync,
    kExceptions,
    kNative,
    kEllipsis,
    kCount, // must be last - number of categories
};

using CategoryMask = uint32_t;

// Каждый элемент - битовая маска категорий для данной группы.
// Индекс = static_cast<uint8_t>(Group).
extern const CategoryMask kGroupCategoryMask[256];

constexpr bool belongsToCategory(Group g, Category c) noexcept {
    return (kGroupCategoryMask[static_cast<uint8_t>(g)] & (1u << static_cast<uint8_t>(c))) != 0;
}

constexpr CategoryMask categoryMask(Group g) noexcept {
    return kGroupCategoryMask[static_cast<uint8_t>(g)];
}

// Единый классификатор числовых групп (целые со знаком/без знака и числа с плавающей
// точкой), используемый типизацией выражений и продвижением (promotion.hpp).
constexpr bool isArithmeticGroup(Group g) noexcept {
    return g == Group::kIntegers || g == Group::kUnsigned || g == Group::kNumbers;
}

} // namespace trust
