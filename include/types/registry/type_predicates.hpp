#pragma once

// Предикаты категории типа (после канонизации алиасов) — единый источник проверок
// «std::any / enum / variant / многомерный массив» для семантики и кодогенерации.

#include "types/type_read.hpp"
#include "types/type_system.hpp"

namespace trust {

// Истина, если TypeId после раскрытия цепочки алиасов (канонизации) относится к
// Group::kAny (std::any). Единый источник проверки «any-операнд» для семантики
// выражений и кодогенерации (std::any_cast). INVALID_TYPE_ID → false.
inline bool isAnyType(TypeId id, const TypeReadModel& reg) noexcept {
    return id != INVALID_TYPE_ID && typeGroupOf(reg.store().desugaredTypeId(id)) == Group::kAny;
}

// Auto — НЕ пользовательский тип; в кодоген не попадает (инвариант). Для совместимости/overload
// ведёт себя как «неизвестно» (принимает и принимается всё, низший ранг kAnyLike).
inline bool isAutoType(TypeId id, const TypeReadModel& reg) noexcept {
    return id != INVALID_TYPE_ID && typeGroupOf(reg.store().desugaredTypeId(id)) == Group::kAuto;
}

// Истина, если TypeId после канонизации относится к Group::kEnums (типобезопасное
// перечисление, EnumTypeData). Единый источник для семантики (резолв Color.RED / Color[k],
// типизация CompareOp) и кодогенерации (эмиссия enum-структуры и операторов). INVALID → false.
inline bool isEnumType(TypeId id, const TypeReadModel& reg) noexcept {
    return id != INVALID_TYPE_ID && typeGroupOf(reg.store().desugaredTypeId(id)) == Group::kEnums;
}

// Истина, если TypeId после канонизации относится к Group::kVariants (гетерогенный вариант,
// VariantTypeData; каждый член своего типа). Единый источник для семантики (резолв Value.RED →
// тип члена) и кодогенерации (эмиссия std::variant<...>). INVALID → false.
inline bool isVariantType(TypeId id, const TypeReadModel& reg) noexcept {
    return id != INVALID_TYPE_ID && typeGroupOf(reg.store().desugaredTypeId(id)) == Group::kVariants;
}

// Истина, если TypeId - структурный Array-тип с НЕСКОЛЬКИМИ размерностями (`:Bool[3,4]`) ИЛИ
// элементный тип сам является массивом (вложенный `[[1,2],[3,4]]`). Многомерные массивы
// регистрируются в реестре, но генерация C++ (тензоры LibTorch) не реализована → диагностика.
inline bool isMultiDimArray(TypeId id, const TypeReadModel& reg) noexcept {
    if (!reg.store().isArrayType(id)) {
        return false;
    }
    const auto& dims = reg.store().arrayDimensions(id);
    const TypeId elem = reg.store().arrayElementType(id);
    return dims.size() > 1 || (elem != INVALID_TYPE_ID && reg.store().isArrayType(elem));
}

} // namespace trust
