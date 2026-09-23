// trust/checked_cast.hpp - checked numeric cast with a range check.
//
// Public runtime header: self-contained (standard headers + trust/assert.hpp),
// embedded into trust-runtime.so/.a (ELF section "trust/checked_cast.hpp") and
// included by generated C++ for explicit user casts `:Type(expr)` (→
// trust::checked_cast<Type>(expr)). Unlike a plain static_cast, out-of-range
// values abort with a runtime diagnostic instead of silently truncating.

#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include "assert.hpp"

namespace trust {

/// Явное приведение с проверкой диапазона. Для целочисленного СТРОГОГО сужения
/// (source шире цели) проверяет, что значение влезает в диапазон цели, иначе -
/// runtime-abort (trust__abort__). Для не-сужений / не-целых - plain static_cast.
template <typename To, typename From>
To checked_cast(From v) {
    if constexpr (std::is_integral_v<To> && std::is_integral_v<From> && (sizeof(From) > sizeof(To))) {
        // Строгое сужение. Нижняя граница применима только когда From знаковый:
        // для беззнакового From отрицательный min<To> заведомо меньше любого v >= 0,
        // а static_cast<From>(min<To>) оборачивается в огромное число - сравнение v < huge
        // сработало бы ложно для всех нормальных значений (баг unsigned → signed).
        if constexpr (std::is_signed_v<From>) {
            if (v < static_cast<From>(std::numeric_limits<To>::min())) {
                trust__abort__(__FILE__, __LINE__, "checked_cast: value out of range for target type");
            }
        }
        if (v > static_cast<From>(std::numeric_limits<To>::max())) {
            trust__abort__(__FILE__, __LINE__, "checked_cast: value out of range for target type");
        }
    }
    return static_cast<To>(v);
}

} // namespace trust
