// trust/rational.hpp — arbitrary-precision rational numbers.
//
// Public runtime header for the Trust runtime. Implemented in
// src/runtime/rational.cpp (part of trust-runtime.so).
//
// This header is self-contained (standard headers only) so that generated C++
// programs can include it without depending on the compiler's include tree.
// At build time it is embedded into trust-runtime.so (via #embed, in an ELF
// section named "trust/rational.hpp"); the pipeline extracts it into a
// temporary `trust/` directory when a program actually uses the Rational type.

#pragma once

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>

namespace trust {

// Arbitrary-precision rational number built on GMP.
// The implementation is hidden behind a pimpl (Impl) to keep the ABI stable and
// to avoid exposing GMP types in this public header.
class Rational {
  public:
    Rational() noexcept;
    Rational(int64_t value) noexcept;
    Rational(std::string_view numerator, std::string_view denominator);
    // Однострочная форма рационального литерала "num\den" (парсинг выполняется внутри).
    Rational(std::string_view value);
    Rational(const Rational& other) noexcept;
    Rational(Rational&& other) noexcept;
    Rational& operator=(Rational other) noexcept;
    ~Rational() noexcept;

    // Conversions
    std::string GetAsString() const;
    // Неявное приведение к std::string (символьное представление "num\den").
    operator std::string() const;
    int64_t GetAsBoolean() const noexcept;
    int64_t GetAsInteger() const;
    double GetAsNumber() const;

    // Accessors / modifiers
    bool isInteger() const noexcept;
    Rational reciprocal() const;
    static Rational abs(const Rational& r) noexcept;

    // Mutating arithmetic
    Rational& operator*=(const Rational& rhs) noexcept;
    Rational& operator/=(const Rational& rhs);
    Rational& operator+=(const Rational& rhs) noexcept;
    Rational& operator-=(const Rational& rhs) noexcept;

    // Comparison (public so the free operators can reuse them)
    bool op_equal(const Rational& rhs) const noexcept;
    int op_compare(const Rational& rhs) const noexcept;

  private:
    // Свободные операторы (в rational.cpp) обращаются к приватным set_() и
    // m_pimpl (унарный минус), поэтому они должны быть друзьями класса.
    friend bool operator==(const Rational& lhs, const Rational& rhs) noexcept;
    friend bool operator!=(const Rational& lhs, const Rational& rhs) noexcept;
    friend bool operator<(const Rational& lhs, const Rational& rhs) noexcept;
    friend bool operator<=(const Rational& lhs, const Rational& rhs) noexcept;
    friend bool operator>(const Rational& lhs, const Rational& rhs) noexcept;
    friend bool operator>=(const Rational& lhs, const Rational& rhs) noexcept;
    friend Rational operator+(Rational lhs, const Rational& rhs) noexcept;
    friend Rational operator-(Rational lhs, const Rational& rhs) noexcept;
    friend Rational operator*(Rational lhs, const Rational& rhs) noexcept;
    friend Rational operator/(Rational lhs, const Rational& rhs);
    friend Rational operator+(const Rational& r) noexcept;
    friend Rational operator-(const Rational& r) noexcept;

    void reduce() noexcept;
    Rational& set_(int64_t value) noexcept;
    Rational& set_(const Rational& copy) noexcept;
    Rational& set_(std::string_view numerator, std::string_view denominator);
    void NormalizeSign() noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_pimpl;
};

// Free operators (declared both as friends above and here for unqualified use).
bool operator==(const Rational& lhs, const Rational& rhs) noexcept;
bool operator!=(const Rational& lhs, const Rational& rhs) noexcept;
bool operator<(const Rational& lhs, const Rational& rhs) noexcept;
bool operator<=(const Rational& lhs, const Rational& rhs) noexcept;
bool operator>(const Rational& lhs, const Rational& rhs) noexcept;
bool operator>=(const Rational& lhs, const Rational& rhs) noexcept;

Rational operator+(Rational lhs, const Rational& rhs) noexcept;
Rational operator-(Rational lhs, const Rational& rhs) noexcept;
Rational operator*(Rational lhs, const Rational& rhs) noexcept;
Rational operator/(Rational lhs, const Rational& rhs);

Rational operator+(const Rational& r) noexcept;
Rational operator-(const Rational& r) noexcept;

} // namespace trust

// std::format support: Rational formats as its symbolic string "num\den"
// (GetAsString). Fill/align/width/type 's' are supported (as for a plain
// string), but *precision* is rejected: for a ratio a precision spec has no
// numeric meaning, and inheriting the string formatter's precision would
// silently truncate the symbolic string (e.g. "{:.2}" → "3\").
template <>
struct std::formatter<trust::Rational> {
    std::formatter<std::string> str_;

    constexpr auto parse(std::format_parse_context& ctx) {
        // Reject precision ('.' not in fill position) before delegating to the
        // string formatter. '.' as the very first char followed by an align char
        // is a *fill* (e.g. "{:.>8}"), not a precision spec.
        auto it = ctx.begin();
        for (auto p = it; p != ctx.end() && *p != '}'; ++p) {
            if (*p == '.') {
                const bool is_fill = (p == it) && (p + 1) != ctx.end() && (*(p + 1) == '<' || *(p + 1) == '>' || *(p + 1) == '^');
                if (!is_fill) {
                    throw std::format_error("precision is not supported for trust::Rational");
                }
            }
        }
        return str_.parse(ctx);
    }

    template <typename FormatContext>
    auto format(const trust::Rational& r, FormatContext& ctx) const {
        return str_.format(r.GetAsString(), ctx);
    }
};
