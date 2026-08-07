// trust/any_convert.hpp — типизированная конверсия из элемента словаря (TypedValue).
//
// Public runtime header. Используется кодогенерацией для `:Type(d.field)` — когда операнд
// каста является элементом словаря (TypedValue: kind + std::any). Категория и размерность
// значения закодированы в kind (TypeKind: Group|Data) и декодируются методами TypedValue —
// автономно, без TypeRegistry:
//   - числовой целевой тип ← kind-число       → checked_cast (контроль диапазона);
//   - std::string          ← kind-строка      → конверсия;
//   - иной целевой тип                          → std::any_cast (точное совпадение);
//   - несовместимое                              → std::runtime_error (не bad_any_cast).
//
// Зависит от trust/dict.hpp (TypedValue) и trust/checked_cast.hpp.

#pragma once

#include <any>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "dict.hpp"
#include "checked_cast.hpp"

namespace trust {

namespace detail {

/// Hex-вывод TypeKind (для диагностики неподдерживаемого каста).
[[nodiscard]] inline std::string kindHex(TypeKind kind) {
    std::string s(8, ' ');
    for (int i = 7; i >= 0; --i) {
        const uint8_t nib = static_cast<uint8_t>(kind >> (i * 4));
        s[7 - i] = nib < 10 ? static_cast<char>('0' + nib) : static_cast<char>('a' + nib - 10);
    }
    return s;
}

/// Извлечение числового значения из TypedValue по TypeKind (число/bool). Не-число → исключение.
[[nodiscard]] inline long double typedToNumber(const TypedValue& tv) {
    if (tv.isBool()) {
        if (const bool* p = std::get_if<bool>(&tv.storage)) {
            return *p ? 1.0L : 0.0L;
        }
    } else if (tv.isInteger()) {
        if (const int64_t* p = std::get_if<int64_t>(&tv.storage)) {
            return static_cast<long double>(*p);
        }
    } else if (tv.isUnsigned()) {
        if (const uint64_t* p = std::get_if<uint64_t>(&tv.storage)) {
            return static_cast<long double>(*p);
        }
    } else if (tv.isFloat()) {
        if (const double* p = std::get_if<double>(&tv.storage)) {
            return static_cast<long double>(*p);
        }
    } else if (tv.isRational()) {
        if (const Rational* p = std::get_if<Rational>(&tv.storage)) {
            return static_cast<long double>(p->GetAsNumber());
        }
    }
    // std::any-ветка (открытые типы / несоответствие) — попытка числовой конверсии.
    if (const std::any* a = std::get_if<std::any>(&tv.storage)) {
        return static_cast<long double>(anyToDouble(*a));
    }
    throw std::runtime_error("trust::any_to: dict element of kind 0x" + kindHex(tv.kind) + " is not numeric");
}

/// Извлечение строки из TypedValue по TypeKind (StrChar→std::string, StrWide→сужение в
/// std::string). Не-строка → исключение.
[[nodiscard]] inline std::string typedToString(const TypedValue& tv) {
    if (tv.isStrChar()) {
        if (const std::string* p = std::get_if<std::string>(&tv.storage)) {
            return *p;
        }
    } else if (tv.isStrWide()) {
        if (const std::wstring* p = std::get_if<std::wstring>(&tv.storage)) {
            return std::string(p->begin(), p->end()); // wide→narrow
        }
    }
    if (const std::any* a = std::get_if<std::any>(&tv.storage)) {
        return anyToString(*a);
    }
    throw std::runtime_error("trust::any_to: dict element of kind 0x" + kindHex(tv.kind) + " is not a string");
}

} // namespace detail

/// Типизированная конверсия из элемента словаря (TypedValue). Поведение зависит от To:
/// числовой (интеграл/float) — извлечение числа по kind + checked_cast; std::string — из
/// строки; иначе — точный std::any_cast.
template <typename To>
[[nodiscard]] To any_to(const TypedValue& tv) {
    if constexpr (std::is_arithmetic_v<To>) {
        return checked_cast<To>(detail::typedToNumber(tv));
    } else if constexpr (std::is_same_v<To, std::string>) {
        return detail::typedToString(tv);
    } else if constexpr (std::is_same_v<To, Rational>) {
        // Rational — быстрая ветка variant (по значению).
        if (const Rational* p = std::get_if<Rational>(&tv.storage)) {
            return *p;
        }
        throw std::bad_any_cast();
    } else {
        // Иной целевой тип: точный std::any_cast из std::any-ветки (открытые типы, Dict и т.п.).
        if (const std::any* a = std::get_if<std::any>(&tv.storage)) {
            return std::any_cast<To>(*a);
        }
        throw std::bad_any_cast();
    }
}

} // namespace trust
