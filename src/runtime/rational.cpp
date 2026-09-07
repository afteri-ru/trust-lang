#include "trust/rational.hpp"
#include "trust/big_integer.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <cstdlib>
#include <cstring>

#include <gmp.h>

namespace trust {

// ============================================================================
// Rational::Impl definition
// ============================================================================
// Числитель и знаменатель - произвольной точности: тип BigInteger (GMP).
// Rational построен "поверх" BigInteger - BigInteger является его составной частью.

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

// ============================================================================
// Rational implementation
// ============================================================================

Rational::Rational() noexcept
: m_pimpl(std::make_unique<Impl>()) {
}

Rational::Rational(int64_t value) noexcept
: m_pimpl(std::make_unique<Impl>(value)) {
}

Rational::Rational(const BigInteger& value) noexcept
: m_pimpl(std::make_unique<Impl>()) {
    // Rational построен на BigInteger: value → value/1.
    m_pimpl->m_numerator = value;
    m_pimpl->m_denominator = BigInteger(1);
}

Rational::Rational(std::string_view numerator, std::string_view denominator)
: m_pimpl(std::make_unique<Impl>()) {
    set_(numerator, denominator);
}

Rational::Rational(std::string_view value)
: m_pimpl(std::make_unique<Impl>()) {
    // Однострочная форма рационального литерала "num\\den": парсинг по обратной косой.
    const std::size_t sep = value.find('\\');
    if (sep == std::string_view::npos) {
        throw std::runtime_error("Rational string must be in form 'num\\den'");
    }
    set_(value.substr(0, sep), value.substr(sep + 1));
}

Rational::Rational(const Rational& other) noexcept
: m_pimpl(std::make_unique<Impl>()) {
    m_pimpl->m_numerator = other.m_pimpl->m_numerator;
    m_pimpl->m_denominator = other.m_pimpl->m_denominator;
}

Rational::Rational(Rational&& other) noexcept = default;

Rational& Rational::operator=(Rational other) noexcept {
    m_pimpl = std::move(other.m_pimpl);
    return *this;
}

Rational::~Rational() noexcept = default;

std::string Rational::GetAsString() const {
    return m_pimpl->m_numerator.GetAsString() + "\\" + m_pimpl->m_denominator.GetAsString();
}

Rational::operator std::string() const {
    return GetAsString();
}

int64_t Rational::GetAsBoolean() const noexcept {
    return !m_pimpl->m_numerator.isZero();
}

int64_t Rational::GetAsInteger() const {
    if (m_pimpl->m_denominator.isZero()) {
        throw std::runtime_error("Denominator must be different from zero!");
    }
    if (m_pimpl->m_denominator.isOne()) {
        return m_pimpl->m_numerator.GetAsInteger();
    }
    auto [q, rem] = m_pimpl->m_numerator.divmod(m_pimpl->m_denominator);
    return q.GetAsInteger();
}

double Rational::GetAsNumber() const {
    if (m_pimpl->m_denominator.isZero()) {
        throw std::runtime_error("Denominator must be different from zero!");
    }
    if (m_pimpl->m_denominator.isOne()) {
        return m_pimpl->m_numerator.GetAsNumber();
    }
    return m_pimpl->m_numerator.GetAsNumber() / m_pimpl->m_denominator.GetAsNumber();
}

void Rational::reduce() noexcept {
    BigInteger gcd = BigInteger::gcd(m_pimpl->m_numerator, m_pimpl->m_denominator);
    if (!gcd.isOne()) {
        m_pimpl->m_numerator = m_pimpl->m_numerator.divideExact(gcd);
        m_pimpl->m_denominator = m_pimpl->m_denominator.divideExact(gcd);
    }
}

Rational& Rational::set_(int64_t value) noexcept {
    m_pimpl->m_numerator = BigInteger(value);
    m_pimpl->m_denominator = BigInteger(1);
    return *this;
}

Rational& Rational::set_(const Rational& copy) noexcept {
    m_pimpl->m_numerator = copy.m_pimpl->m_numerator;
    m_pimpl->m_denominator = copy.m_pimpl->m_denominator;
    return *this;
}

Rational& Rational::set_(std::string_view numerator, std::string_view denominator) {
    m_pimpl->m_numerator = BigInteger(numerator);
    m_pimpl->m_denominator = BigInteger(denominator);
    if (m_pimpl->m_denominator.isZero()) {
        throw std::runtime_error("Denominator cannot be zero");
    }
    NormalizeSign();
    reduce();
    return *this;
}

void Rational::NormalizeSign() noexcept {
    if (m_pimpl->m_denominator.isNegative()) {
        m_pimpl->m_numerator.negate();
        m_pimpl->m_denominator.negate();
    }
    if (m_pimpl->m_numerator.isZero() && !m_pimpl->m_denominator.isOne()) {
        m_pimpl->m_denominator = BigInteger(1);
    }
}

bool Rational::isInteger() const noexcept {
    return m_pimpl->m_denominator.isOne();
}

Rational Rational::reciprocal() const {
    if (m_pimpl->m_numerator.isZero()) {
        throw std::runtime_error("Cannot compute reciprocal of zero");
    }
    Rational result;
    result.m_pimpl->m_numerator = m_pimpl->m_denominator;
    result.m_pimpl->m_denominator = m_pimpl->m_numerator;
    result.NormalizeSign();
    return result;
}

Rational Rational::abs(const Rational& r) noexcept {
    if (r.m_pimpl->m_numerator.isNegative()) {
        Rational result;
        result.m_pimpl->m_numerator = r.m_pimpl->m_numerator;
        result.m_pimpl->m_numerator.negate();
        result.m_pimpl->m_denominator = r.m_pimpl->m_denominator;
        return result;
    }
    return r;
}

Rational& Rational::operator*=(const Rational& rhs) noexcept {
    m_pimpl->m_numerator *= rhs.m_pimpl->m_numerator;
    m_pimpl->m_denominator *= rhs.m_pimpl->m_denominator;
    NormalizeSign();
    reduce();
    return *this;
}

Rational& Rational::operator/=(const Rational& rhs) {
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

Rational& Rational::operator+=(const Rational& rhs) noexcept {
    BigInteger add_num(rhs.m_pimpl->m_numerator);
    add_num *= m_pimpl->m_denominator;
    m_pimpl->m_numerator *= rhs.m_pimpl->m_denominator;
    m_pimpl->m_denominator *= rhs.m_pimpl->m_denominator;
    m_pimpl->m_numerator += add_num;
    NormalizeSign();
    reduce();
    return *this;
}

Rational& Rational::operator-=(const Rational& rhs) noexcept {
    BigInteger sub_num(rhs.m_pimpl->m_numerator);
    sub_num *= m_pimpl->m_denominator;
    m_pimpl->m_numerator *= rhs.m_pimpl->m_denominator;
    m_pimpl->m_denominator *= rhs.m_pimpl->m_denominator;
    m_pimpl->m_numerator -= sub_num;
    NormalizeSign();
    reduce();
    return *this;
}

bool Rational::op_equal(const Rational& rhs) const noexcept {
    return op_compare(rhs) == 0;
}

int Rational::op_compare(const Rational& rhs) const noexcept {
    BigInteger left = m_pimpl->m_numerator * rhs.m_pimpl->m_denominator;
    BigInteger right = rhs.m_pimpl->m_numerator * m_pimpl->m_denominator;
    return left.op_compare(right);
}
// ============================================================================
// Free-standing operators
// ============================================================================

bool operator==(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_equal(rhs);
}

bool operator!=(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) != 0;
}

bool operator<(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) < 0;
}

bool operator<=(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) <= 0;
}

bool operator>(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) > 0;
}

bool operator>=(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_compare(rhs) >= 0;
}

Rational operator+(Rational lhs, const Rational& rhs) noexcept {
    return lhs += rhs;
}

Rational operator-(Rational lhs, const Rational& rhs) noexcept {
    return lhs -= rhs;
}

Rational operator*(Rational lhs, const Rational& rhs) noexcept {
    return lhs *= rhs;
}

Rational operator/(Rational lhs, const Rational& rhs) {
    return lhs /= rhs;
}

Rational operator+(const Rational& r) noexcept {
    return r;
}

Rational operator-(const Rational& r) noexcept {
    Rational result;
    result.set_(r);
    result.m_pimpl->m_numerator.negate();
    return result;
}

} // namespace trust
