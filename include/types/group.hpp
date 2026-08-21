#pragma once

#include <cstdint>
#include <string_view>

#include "types/typekind.hpp"

namespace trust {

// -- Group (0..255) ---------------------------------------
// Плоский enum всех групп.
// Any=0 (корень всех типов), Void=1 (Void/None).
// Остальные - автоматически.
enum class Group : uint8_t {
    kAny = 0,  // Корень всех типов (Data=0, registry)
    kVoid = 1, // Void(Data=1), None(Data=2)

    // -- Встроенные группы (Data≠0 для конкретных) ------
    kLogical,   // Bool(Data=1)           - 2
    kIntegers,  // 8,16,32,64             - 3
    kUnsigned,  // 8,16,32,64             - 4
    kNumbers,   // 16,32,64               - 5
    kBFloat,    // 16                     - 6
    kComplex,   // 32,64                  - 7
    kRationals, // 1                      - 8
    kStrChar,   // 1                      - 9
    kStrWide,   // 1                      - 10
    kDicts,     // 1                      - 11

    // -- Группы для реестра (Data=0) ---------------------
    kTensors,       // 12
    kContainers,    // 13
    kStructured,    // 14
    kCallable,      // 15
    kClasses,       // 16
    kRanges,        // 17
    kIterators,     // 18
    kDateTime,      // 19
    kAsync,         // 20
    kSync,          // 21
    kExceptions,    // 22
    kNative,        // 23
    kEllipsis,      // 24
    kArithmetics,   // 25
    kTemplateParam, // 26 - template type parameter (data=depth)
    kReftype,       // 27 - ссылочные/указательные составные типы (реестр, Data≠0, RefType=вид)
    kEnums,         // 28 - типобезопасные перечисления (реестр, EnumTypeData)
    kVariants,      // 29 - гетерогенные варианты (реестр, VariantTypeData; → std::variant)
};

// -- Category (≤ 32 категорий) ----------------------------
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

// -- Group → Category таблица -----------------------------
// Каждый элемент - битовая маска категорий для данной группы.
// Индекс = static_cast<uint8_t>(Group).
extern const CategoryMask kGroupCategoryMask[256];

// -- Проверка принадлежности ------------------------------
constexpr bool belongsToCategory(Group g, Category c) noexcept {
    return (kGroupCategoryMask[static_cast<uint8_t>(g)] & (1u << static_cast<uint8_t>(c))) != 0;
}

// -- Маска категорий для группы ---------------------------
constexpr CategoryMask categoryMask(Group g) noexcept {
    return kGroupCategoryMask[static_cast<uint8_t>(g)];
}

// -- Арифметические группы --------------------------------
// Единый классификатор числовых групп (целые со знаком/без знака и числа с плавающей
// точкой), используемый типизацией выражений и продвижением (promotion.hpp).
constexpr bool isArithmeticGroup(Group g) noexcept {
    return g == Group::kIntegers || g == Group::kUnsigned || g == Group::kNumbers;
}

} // namespace trust
