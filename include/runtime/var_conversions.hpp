#pragma once

#include <concepts>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "runtime/rational.hpp"
#include "diag/error.hpp"

namespace trust {

class TensorHandle;

// ============================================================================
// Conversion functions — replace old VarConversions class
//
// Base conversions:
//   - format_as_string<T> — generic to_string via std::format (specialize if needed)
//   - parse_numeric<T> — generic from string via std::from_chars/stod/etc
//   - numeric::cross_convert<Tgt, Src> — concept-based cast (eliminates manual specializations)
//
// All as_impl<T>() functions call these — no duplicated switch bodies needed.
// ============================================================================

// ---- format_as_string: generic numeric->string ----
template <typename T>
[[nodiscard]] std::string format_as_string(const T &v) {
    return std::format("{}", v);
}

// Rational needs custom formatting (GetAsString)
template <>
[[nodiscard]] std::string format_as_string<Rational>(const Rational &v);

// Tensor string representation
[[nodiscard]] std::string format_as_tensor_as_string(const TensorHandle &h);

// ---- parse_numeric: string->numeric (parameterized) ----
// Integral types use std::from_chars; floating types use std::stod
template <std::integral T>
[[nodiscard]] inline T parse_numeric(std::string_view s) {
    T result{};
    const auto *begin = s.data();
    const auto *end = begin + s.size();
    const auto [ptr, ec] = std::from_chars(begin, end, result);
    if (ec != std::errc{0} || ptr != end) {
        FAULT_AS(ParseError, "Cannot convert string '{}' to numeric: {}", s, std::make_error_code(ec).message());
    }
    return result;
}

template <std::floating_point T>
[[nodiscard]] inline T parse_numeric(std::string_view s) {
    try {
        return static_cast<T>(std::stod(std::string(s)));
    } catch (std::exception const &e) {
        FAULT_AS(ParseError, "Cannot convert string '{}' to floating: {}", s, e.what());
    }
}

// Bool parsing (non-numeric, kept separate)
[[nodiscard]] bool parse_as_bool(std::string_view s);

// ---- numeric::cross_convert — concept-based, no manual specializations ----
namespace numeric {

// Universal numeric conversion: static_cast handles all cases
template <typename To, typename From>
    requires(!std::same_as<std::remove_cvref_t<From>, bool> && (std::integral<To> || std::floating_point<To>) &&
             (std::integral<From> || std::floating_point<From>))
[[nodiscard]] constexpr To cross_convert(From v) {
    return static_cast<To>(v);
}

// bool -> numeric: explicit 0/1 conversion
template <typename To>
    requires(std::integral<To> || std::floating_point<To>)
[[nodiscard]] constexpr To cross_convert_from_bool(bool v) {
    return v ? To{1} : To{0};
}

// numeric -> bool: non-zero check
template <typename From>
    requires(std::integral<From> || std::floating_point<From>)
[[nodiscard]] constexpr bool cross_convert_to_bool(From v) {
    return v != From{0};
}

} // namespace numeric

// ---- widen: string -> wstring (used in as<std::wstring>) ----
[[nodiscard]] std::wstring widen_string(const std::string &s);

// ---- narrow: wstring -> string ----
[[nodiscard]] std::string narrow_string(const std::wstring &s);

} // namespace trust