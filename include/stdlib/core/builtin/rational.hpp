// stdlib/builtin/rational.hpp - arbitrary-precision rational numbers.
// Public header of the built-in Rational type. Header-only (inline): the GMP-backed
// implementation lives HERE, not in a compiled runtime TU, so the type is delivered
// entirely by the standard library package and needs only -lgmp at link time.
// Delivery: package file `include/stdlib/builtin/rational.hpp` inside share/trust/stdlib.trust
// (available via -Istdlib/include).

#pragma once

#include "big_integer.hpp"

#include <cstdint>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace trust {

// Rational построен на BigInteger; определение последнего даёт этот же заголовок.

// Arbitrary-precision rational number built on GMP.
// The implementation is hidden behind a pimpl (Impl) to keep the ABI stable and
// to avoid exposing GMP types in this public header.
class Rational {
  public:
    Rational() noexcept;
    Rational(int64_t value) noexcept;
    // BigInteger → Rational(value/1): неявная конверсия для смешанной арифметики
    // (BigInteger + Rational, Rational + BigInteger) и присваивания Rational := BigInteger.
    Rational(const BigInteger& value) noexcept;
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
    // Сокращённое представление для показа: каждый компонент num\den сокращается по правилам
    // BigInteger::display(L) (голова = хвост = L, при необходимости добавляется " (N)").
    // Только для вывода: результат не парсится обратно. L < 1 - исключение std::invalid_argument.
    std::string display(int64_t head_tail) const;

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
    // Свободные операторы (ниже, inline) обращаются к приватным set_() и
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

// Header-only implementation (previously src/runtime/rational.cpp)
// Числитель и знаменатель - произвольной точности: тип BigInteger (GMP).
// Rational построен "поверх" BigInteger.

class Rational::Impl {
  public:
    BigInteger m_numerator;
    BigInteger m_denominator;

    Impl() noexcept
    : m_numerator(0)
    , m_denominator(1) {}
    explicit Impl(int64_t value) noexcept
    : m_numerator(value)
    , m_denominator(1) {}
};

inline Rational::Rational() noexcept
: m_pimpl(std::make_unique<Impl>()) {
}

inline Rational::Rational(int64_t value) noexcept
: m_pimpl(std::make_unique<Impl>(value)) {
}

inline Rational::Rational(const BigInteger& value) noexcept
: m_pimpl(std::make_unique<Impl>()) {
    // Rational построен на BigInteger: value → value/1.
    m_pimpl->m_numerator = value;
    m_pimpl->m_denominator = BigInteger(1);
}

inline Rational::Rational(std::string_view numerator, std::string_view denominator)
: m_pimpl(std::make_unique<Impl>()) {
    set_(numerator, denominator);
}

inline Rational::Rational(std::string_view value)
: m_pimpl(std::make_unique<Impl>()) {
    // Однострочная форма рационального литерала "num\\den": парсинг по обратной косой.
    const std::size_t sep = value.find('\\');
    if (sep == std::string_view::npos) {
        throw std::runtime_error("Rational string must be in form 'num\\den'");
    }
    set_(value.substr(0, sep), value.substr(sep + 1));
}

inline Rational::Rational(const Rational& other) noexcept
: m_pimpl(std::make_unique<Impl>()) {
    m_pimpl->m_numerator = other.m_pimpl->m_numerator;
    m_pimpl->m_denominator = other.m_pimpl->m_denominator;
}

inline Rational::Rational(Rational&& other) noexcept = default;

inline Rational& Rational::operator=(Rational other) noexcept {
    m_pimpl = std::move(other.m_pimpl);
    return *this;
}

inline Rational::~Rational() noexcept = default;

inline std::string Rational::GetAsString() const {
    return m_pimpl->m_numerator.GetAsString() + "\\" + m_pimpl->m_denominator.GetAsString();
}

inline std::string Rational::display(int64_t head_tail) const {
    // Единый источник правды: каждый компонент сокращается BigInteger::display (L < 1 → ошибка).
    return m_pimpl->m_numerator.display(head_tail) + "\\" + m_pimpl->m_denominator.display(head_tail);
}

inline Rational::operator std::string() const {
    return GetAsString();
}

inline int64_t Rational::GetAsBoolean() const noexcept {
    return !m_pimpl->m_numerator.isZero();
}

inline int64_t Rational::GetAsInteger() const {
    if (m_pimpl->m_denominator.isZero()) {
        throw std::runtime_error("Denominator must be different from zero!");
    }
    if (m_pimpl->m_denominator.isOne()) {
        return m_pimpl->m_numerator.GetAsInteger();
    }
    auto [q, rem] = m_pimpl->m_numerator.divmod(m_pimpl->m_denominator);
    return q.GetAsInteger();
}

inline double Rational::GetAsNumber() const {
    if (m_pimpl->m_denominator.isZero()) {
        throw std::runtime_error("Denominator must be different from zero!");
    }
    if (m_pimpl->m_denominator.isOne()) {
        return m_pimpl->m_numerator.GetAsNumber();
    }
    return m_pimpl->m_numerator.GetAsNumber() / m_pimpl->m_denominator.GetAsNumber();
}

inline void Rational::reduce() noexcept {
    BigInteger gcd = BigInteger::gcd(m_pimpl->m_numerator, m_pimpl->m_denominator);
    if (!gcd.isOne()) {
        m_pimpl->m_numerator = m_pimpl->m_numerator.divideExact(gcd);
        m_pimpl->m_denominator = m_pimpl->m_denominator.divideExact(gcd);
    }
}

inline Rational& Rational::set_(int64_t value) noexcept {
    m_pimpl->m_numerator = BigInteger(value);
    m_pimpl->m_denominator = BigInteger(1);
    return *this;
}

inline Rational& Rational::set_(const Rational& copy) noexcept {
    m_pimpl->m_numerator = copy.m_pimpl->m_numerator;
    m_pimpl->m_denominator = copy.m_pimpl->m_denominator;
    return *this;
}

inline Rational& Rational::set_(std::string_view numerator, std::string_view denominator) {
    m_pimpl->m_numerator = BigInteger(numerator);
    m_pimpl->m_denominator = BigInteger(denominator);
    if (m_pimpl->m_denominator.isZero()) {
        throw std::runtime_error("Denominator cannot be zero");
    }
    NormalizeSign();
    reduce();
    return *this;
}

inline void Rational::NormalizeSign() noexcept {
    if (m_pimpl->m_denominator.isNegative()) {
        m_pimpl->m_numerator.negate();
        m_pimpl->m_denominator.negate();
    }
    if (m_pimpl->m_numerator.isZero() && !m_pimpl->m_denominator.isOne()) {
        m_pimpl->m_denominator = BigInteger(1);
    }
}

inline bool Rational::isInteger() const noexcept {
    return m_pimpl->m_denominator.isOne();
}

inline Rational Rational::reciprocal() const {
    if (m_pimpl->m_numerator.isZero()) {
        throw std::runtime_error("Cannot compute reciprocal of zero");
    }
    Rational result;
    result.m_pimpl->m_numerator = m_pimpl->m_denominator;
    result.m_pimpl->m_denominator = m_pimpl->m_numerator;
    result.NormalizeSign();
    return result;
}

inline Rational Rational::abs(const Rational& r) noexcept {
    if (r.m_pimpl->m_numerator.isNegative()) {
        Rational result;
        result.m_pimpl->m_numerator = r.m_pimpl->m_numerator;
        result.m_pimpl->m_numerator.negate();
        result.m_pimpl->m_denominator = r.m_pimpl->m_denominator;
        return result;
    }
    return r;
}

inline Rational& Rational::operator*=(const Rational& rhs) noexcept {
    m_pimpl->m_numerator *= rhs.m_pimpl->m_numerator;
    m_pimpl->m_denominator *= rhs.m_pimpl->m_denominator;
    NormalizeSign();
    reduce();
    return *this;
}

inline Rational& Rational::operator/=(const Rational& rhs) {
    if (rhs.m_pimpl->m_numerator.isZero()) {
        throw std::runtime_error("Division by zero");
    }
    BigInteger new_num(m_pimpl->m_numerator);
    new_num *= rhs.m_pimpl->m_denominator;
    m_pimpl->m_denominator *= rhs.m_pimpl->m_numerator;
    m_pimpl->m_numerator = std::move(new_num);
    NormalizeSign();
    reduce();
    return *this;
}

inline Rational& Rational::operator+=(const Rational& rhs) noexcept {
    BigInteger add_num(rhs.m_pimpl->m_numerator);
    add_num *= m_pimpl->m_denominator;
    m_pimpl->m_numerator *= rhs.m_pimpl->m_denominator;
    m_pimpl->m_denominator *= rhs.m_pimpl->m_denominator;
    m_pimpl->m_numerator += add_num;
    NormalizeSign();
    reduce();
    return *this;
}

inline Rational& Rational::operator-=(const Rational& rhs) noexcept {
    BigInteger sub_num(rhs.m_pimpl->m_numerator);
    sub_num *= m_pimpl->m_denominator;
    m_pimpl->m_numerator *= rhs.m_pimpl->m_denominator;
    m_pimpl->m_denominator *= rhs.m_pimpl->m_denominator;
    m_pimpl->m_numerator -= sub_num;
    NormalizeSign();
    reduce();
    return *this;
}

inline bool Rational::op_equal(const Rational& rhs) const noexcept {
    return op_compare(rhs) == 0;
}

inline int Rational::op_compare(const Rational& rhs) const noexcept {
    BigInteger left = m_pimpl->m_numerator * rhs.m_pimpl->m_denominator;
    BigInteger right = rhs.m_pimpl->m_numerator * m_pimpl->m_denominator;
    return left.op_compare(right);
}

// Free operators (declared both as friends above and here for unqualified use).
inline bool operator==(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_equal(rhs);
}

inline bool operator!=(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) != 0;
}

inline bool operator<(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) < 0;
}

inline bool operator<=(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) <= 0;
}

inline bool operator>(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) > 0;
}

inline bool operator>=(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) >= 0;
}

inline Rational operator+(Rational lhs, const Rational& rhs) noexcept {
    return lhs += rhs;
}

inline Rational operator-(Rational lhs, const Rational& rhs) noexcept {
    return lhs -= rhs;
}

inline Rational operator*(Rational lhs, const Rational& rhs) noexcept {
    return lhs *= rhs;
}

inline Rational operator/(Rational lhs, const Rational& rhs) {
    return lhs /= rhs;
}

inline Rational operator+(const Rational& r) noexcept {
    return r;
}

inline Rational operator-(const Rational& r) noexcept {
    Rational result;
    result.set_(r);
    result.m_pimpl->m_numerator.negate();
    return result;
}

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
