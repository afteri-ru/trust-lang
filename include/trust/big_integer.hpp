// trust/big_integer.hpp - arbitrary-precision integers.
//
// Public runtime header for the Trust runtime. Implemented in
// src/runtime/big_integer.cpp (part of trust-runtime.so/.a).
//
// This header is self-contained (standard headers only) so that generated C++
// programs can include it without depending on the compiler's include tree.
// At build time it is embedded into trust-runtime.so (via #embed, in an ELF
// section named "trust/big_integer.hpp"); the pipeline extracts it into a
// temporary `trust/` directory when a program actually uses the BigInteger type.

#pragma once

#include <compare>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace trust {

// Arbitrary-precision (signed) integer built on GMP. This is the "big number"
// part that Rational is built on: Rational's numerator and denominator are
// BigInteger values.
// The implementation is hidden behind a pimpl (Impl) to keep the ABI stable and
// to avoid exposing GMP types in this public header.
class BigInteger {
  public:
    BigInteger() noexcept;
    BigInteger(int64_t value) noexcept;
    // Десятичный литерал-строка, напр. "123", "-9223372036854775808".
    BigInteger(std::string_view value);
    BigInteger(const BigInteger& other) noexcept;
    BigInteger(BigInteger&& other) noexcept;
    BigInteger& operator=(BigInteger other) noexcept;
    ~BigInteger() noexcept;

    // Conversions
    std::string GetAsString() const;
    // Неявное приведение к std::string (десятичное представление).
    operator std::string() const;
    int64_t GetAsBoolean() const noexcept;
    // Приводит к int64_t; кидает std::overflow_error, если значение не влезает.
    int64_t GetAsInteger() const;
    double GetAsNumber() const noexcept;

    // Predicates
    bool isZero() const noexcept;
    bool isOne() const noexcept;
    bool isNegative() const noexcept;

    // Comparison (public so the free operators can reuse them)
    bool op_equal(const BigInteger& rhs) const noexcept;
    int op_compare(const BigInteger& rhs) const noexcept;

    // Mutating arithmetic. operator/= throws std::runtime_error on division by zero.
    BigInteger& operator+=(const BigInteger& rhs) noexcept;
    BigInteger& operator-=(const BigInteger& rhs) noexcept;
    BigInteger& operator*=(const BigInteger& rhs) noexcept;
    BigInteger& operator/=(const BigInteger& rhs);

    // Helpers used by Rational (arbitrary-precision rational built on BigInteger).
    void negate() noexcept;
    // Делёж с остатком по floor (как mpz_fdiv_qr) - как Rational::GetAsInteger.
    std::pair<BigInteger, BigInteger> divmod(const BigInteger& rhs) const;
    // Точное деление (rhs обязан делить нацело), как mpz_divexact.
    BigInteger divideExact(const BigInteger& rhs) const;
    static BigInteger gcd(const BigInteger& a, const BigInteger& b) noexcept;

  private:
    // Свободные операторы (в big_integer.cpp) обращаются к приватным set_()/m_pimpl
    // (унарный минус), поэтому объявлены друзьями. Сравнение (operator==/<=>) использует
    // публичные op_equal/op_compare - дружба не нужна.
    friend BigInteger operator+(BigInteger lhs, const BigInteger& rhs) noexcept;
    friend BigInteger operator-(BigInteger lhs, const BigInteger& rhs) noexcept;
    friend BigInteger operator*(BigInteger lhs, const BigInteger& rhs) noexcept;
    friend BigInteger operator/(BigInteger lhs, const BigInteger& rhs);
    friend BigInteger operator+(const BigInteger& r) noexcept;
    friend BigInteger operator-(const BigInteger& r) noexcept;

    BigInteger& set_(int64_t value) noexcept;
    BigInteger& set_(const BigInteger& copy) noexcept;
    BigInteger& set_(std::string_view value);

    struct Impl;
    std::unique_ptr<Impl> m_pimpl;
};

// Free operators. Сравнение реализовано через operator== и operator<=> (C++20): из них
// компилятор синтезирует != (из ==) и <, <=, >, >= (из <=>) - без копипаста шести операторов.
bool operator==(const BigInteger& lhs, const BigInteger& rhs) noexcept;
std::strong_ordering operator<=>(const BigInteger& lhs, const BigInteger& rhs) noexcept;

BigInteger operator+(BigInteger lhs, const BigInteger& rhs) noexcept;
BigInteger operator-(BigInteger lhs, const BigInteger& rhs) noexcept;
BigInteger operator*(BigInteger lhs, const BigInteger& rhs) noexcept;
BigInteger operator/(BigInteger lhs, const BigInteger& rhs);

BigInteger operator+(const BigInteger& r) noexcept;
BigInteger operator-(const BigInteger& r) noexcept;

} // namespace trust

// std::format support: BigInteger formats as its decimal string (GetAsString).
// Fill/align/width/type 's' are supported (as for a plain string), but
// *precision* is rejected: for an integer a precision spec has no numeric
// meaning, and inheriting the string formatter's precision would silently
// truncate the decimal string.
template <>
struct std::formatter<trust::BigInteger> {
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
                    throw std::format_error("precision is not supported for trust::BigInteger");
                }
            }
        }
        return str_.parse(ctx);
    }

    template <typename FormatContext>
    auto format(const trust::BigInteger& v, FormatContext& ctx) const {
        return str_.format(v.GetAsString(), ctx);
    }
};
