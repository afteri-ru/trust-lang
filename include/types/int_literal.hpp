#pragma once

// include/types/int_literal.hpp
// Единый источник диапазонов целых литералов и соответствия ширина↔тип.
// Вынесен в types из semantic/type_inference.hpp, чтобы границы целых жили в одном месте
// рядом с реестром типов (TypeRegistry хранит ширину data для встроенных типов, но НЕ
// границы диапазонов и НЕ «тип по ширине»). Используется:
//   - literalType (выбор минимального вмещающего знакового Int по значению литерала);
//   - intFitsTarget / checkAssignmentNarrowing (влезает ли литерал в целевой целый тип).

#include "types/group.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"

#include <cstdint>

namespace trust {

// -- Вмещаемость беззнакового значения в целый тип ----------
// kIntegers → 2^(w-1)-1, kUnsigned → 2^w-1. Не-целая группа → значение считается безопасным.
inline bool fitsIntegerValue(Group g, uint8_t width, unsigned long long value) noexcept {
    if (g == Group::kIntegers) {
        switch (width) {
        case 8:
            return value <= 127ULL;
        case 16:
            return value <= 32767ULL;
        case 32:
            return value <= 2147483647ULL;
        case 64:
        default:
            return true;
        }
    }
    if (g == Group::kUnsigned) {
        switch (width) {
        case 8:
            return value <= 255ULL;
        case 16:
            return value <= 65535ULL;
        case 32:
            return value <= 4294967295ULL;
        case 64:
        default:
            return true;
        }
    }
    return true; // не-целая группа - целочисленный литерал считается безопасным
}

// -- Знаковый целый тип (Int8/16/32/64) по ширине в битах ----
inline TypeId intTypeForWidth(const TypeRegistry& reg, uint8_t width) {
    switch (width) {
    case 8:
        return reg.getType(type::Int8);
    case 16:
        return reg.getType(type::Int16);
    case 32:
        return reg.getType(type::Int32);
    default:
        return reg.getType(type::Int64);
    }
}

// -- Минимальный знаковый Int, вмещающий значение -----------
// Единая таблица ширины/границ для выбора типа целого литерала (literalType).
inline TypeId intTypeForLiteral(const TypeRegistry& reg, unsigned long long value) {
    constexpr uint8_t kSignedWidths[] = {8, 16, 32, 64};
    for (const uint8_t w : kSignedWidths) {
        if (fitsIntegerValue(Group::kIntegers, w, value)) {
            return intTypeForWidth(reg, w);
        }
    }
    return reg.getType(type::Int64);
}

} // namespace trust
