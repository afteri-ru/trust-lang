#include "runtime/rational.hpp"

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
// BigNum implementation (hidden)
// ============================================================================

struct BigNum {
    mpz_t value;

    BigNum() noexcept { mpz_init(value); }
    ~BigNum() noexcept { mpz_clear(value); }

    explicit BigNum(int64_t var) noexcept {
        mpz_init(value);
        mpz_set_si(value, var);
    }

    explicit BigNum(std::string_view str) {
        mpz_init(value);
        SetFromString(str);
    }

    BigNum(const BigNum &other) { mpz_init_set(value, other.value); }

    BigNum(BigNum &&other) noexcept {
        mpz_init(value);
        mpz_swap(value, other.value);
    }

    BigNum &operator=(BigNum other) noexcept {
        mpz_swap(value, other.value);
        return *this;
    }

    BigNum &operator=(int64_t var) noexcept {
        mpz_set_si(value, var);
        return *this;
    }

    int64_t GetAsInteger() const {
        if (isOverflow()) {
            throw std::overflow_error("BigNum integer overflow!");
        }
        return mpz_get_si(value);
    }

    double GetAsNumber() const noexcept { return mpz_get_d(value); }

    std::string GetAsString() const {
        char *ptr = mpz_get_str(nullptr, 10, value);
        std::string result(ptr);
        std::free(ptr);
        return result;
    }

    BigNum &add(const BigNum &val) noexcept {
        mpz_add(value, value, val.value);
        return *this;
    }

    BigNum &sub(const BigNum &val) noexcept {
        mpz_sub(value, value, val.value);
        return *this;
    }

    BigNum &mul(const BigNum &val) noexcept {
        mpz_mul(value, value, val.value);
        return *this;
    }

    BigNum &div(const BigNum &val, BigNum &rem) noexcept {
        mpz_fdiv_qr(value, rem.value, value, val.value);
        return *this;
    }

    bool SetFromString(std::string_view str) {
        if (str.empty()) {
            throw std::invalid_argument("String cannot be empty");
        }
        std::string temp(str);
        int result = mpz_set_str(value, temp.c_str(), 10);
        if (result != 0) {
            throw std::runtime_error("Failed to create BigNum from string '" + std::string(str) + "'");
        }
        return true;
    }

    void SetOne() noexcept { mpz_set_ui(value, 1); }
    void SetZero() noexcept { mpz_set_ui(value, 0); }

    bool isOverflow() const noexcept { return mpz_sizeinbase(value, 2) > 63; }
    bool isZero() const noexcept { return mpz_sgn(value) == 0; }
    bool isOne() const noexcept { return mpz_cmp_ui(value, 1) == 0; }
    bool isNegative() const noexcept { return mpz_sgn(value) < 0; }
};

// ============================================================================
// Rational::Impl definition
// ============================================================================

class Rational::Impl {
  public:
    BigNum m_numerator;
    BigNum m_denominator;

    Impl() noexcept : m_numerator(0), m_denominator(1) {}
    explicit Impl(int64_t value) noexcept : m_numerator(value), m_denominator(1) {}
};

// ============================================================================
// Rational implementation
// ============================================================================

Rational::Rational() noexcept : m_pimpl(std::make_unique<Impl>()) {
}

Rational::Rational(int64_t value) noexcept : m_pimpl(std::make_unique<Impl>(value)) {
}

Rational::Rational(std::string_view numerator, std::string_view denominator) : m_pimpl(std::make_unique<Impl>()) {
    set_(numerator, denominator);
}

Rational::Rational(const Rational &other) noexcept : m_pimpl(std::make_unique<Impl>()) {
    m_pimpl->m_numerator = other.m_pimpl->m_numerator;
    m_pimpl->m_denominator = other.m_pimpl->m_denominator;
}

Rational::Rational(Rational &&other) noexcept = default;

Rational &Rational::operator=(Rational other) noexcept {
    m_pimpl = std::move(other.m_pimpl);
    return *this;
}

Rational::~Rational() noexcept = default;

std::string Rational::GetAsString() const {
    return m_pimpl->m_numerator.GetAsString() + "\\" + m_pimpl->m_denominator.GetAsString();
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
    BigNum result(m_pimpl->m_numerator);
    BigNum rem;
    result.div(m_pimpl->m_denominator, rem);
    return result.GetAsInteger();
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
    BigNum gcd;
    mpz_gcd(gcd.value, m_pimpl->m_numerator.value, m_pimpl->m_denominator.value);
    if (!gcd.isOne()) {
        mpz_divexact(m_pimpl->m_numerator.value, m_pimpl->m_numerator.value, gcd.value);
        mpz_divexact(m_pimpl->m_denominator.value, m_pimpl->m_denominator.value, gcd.value);
    }
}

Rational &Rational::set_(int64_t value) noexcept {
    m_pimpl->m_numerator = BigNum(value);
    m_pimpl->m_denominator.SetOne();
    return *this;
}

Rational &Rational::set_(const Rational &copy) noexcept {
    m_pimpl->m_numerator = copy.m_pimpl->m_numerator;
    m_pimpl->m_denominator = copy.m_pimpl->m_denominator;
    return *this;
}

Rational &Rational::set_(std::string_view numerator, std::string_view denominator) {
    m_pimpl->m_numerator.SetFromString(numerator);
    m_pimpl->m_denominator.SetFromString(denominator);
    if (m_pimpl->m_denominator.isZero()) {
        throw std::runtime_error("Denominator cannot be zero");
    }
    NormalizeSign();
    reduce();
    return *this;
}

void Rational::NormalizeSign() noexcept {
    if (m_pimpl->m_denominator.isNegative()) {
        mpz_neg(m_pimpl->m_numerator.value, m_pimpl->m_numerator.value);
        mpz_neg(m_pimpl->m_denominator.value, m_pimpl->m_denominator.value);
    }
    if (m_pimpl->m_numerator.isZero() && !m_pimpl->m_denominator.isOne()) {
        m_pimpl->m_denominator.SetOne();
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

Rational Rational::abs(const Rational &r) noexcept {
    if (r.m_pimpl->m_numerator.isNegative()) {
        Rational result;
        mpz_neg(result.m_pimpl->m_numerator.value, r.m_pimpl->m_numerator.value);
        result.m_pimpl->m_denominator = r.m_pimpl->m_denominator;
        return result;
    }
    return r;
}

Rational &Rational::operator*=(const Rational &rhs) noexcept {
    m_pimpl->m_numerator.mul(rhs.m_pimpl->m_numerator);
    m_pimpl->m_denominator.mul(rhs.m_pimpl->m_denominator);
    NormalizeSign();
    reduce();
    return *this;
}

Rational &Rational::operator/=(const Rational &rhs) {
    if (rhs.m_pimpl->m_numerator.isZero()) {
        throw std::runtime_error("Division by zero");
    }
    BigNum new_num(m_pimpl->m_numerator);
    new_num.mul(rhs.m_pimpl->m_denominator);
    m_pimpl->m_denominator.mul(rhs.m_pimpl->m_numerator);
    m_pimpl->m_numerator = std::move(new_num);
    NormalizeSign();
    reduce();
    return *this;
}

Rational &Rational::operator+=(const Rational &rhs) noexcept {
    BigNum add_num(rhs.m_pimpl->m_numerator);
    add_num.mul(m_pimpl->m_denominator);
    m_pimpl->m_numerator.mul(rhs.m_pimpl->m_denominator);
    m_pimpl->m_denominator.mul(rhs.m_pimpl->m_denominator);
    m_pimpl->m_numerator.add(add_num);
    NormalizeSign();
    reduce();
    return *this;
}

Rational &Rational::operator-=(const Rational &rhs) noexcept {
    BigNum sub_num(rhs.m_pimpl->m_numerator);
    sub_num.mul(m_pimpl->m_denominator);
    m_pimpl->m_numerator.mul(rhs.m_pimpl->m_denominator);
    m_pimpl->m_denominator.mul(rhs.m_pimpl->m_denominator);
    m_pimpl->m_numerator.sub(sub_num);
    NormalizeSign();
    reduce();
    return *this;
}

bool Rational::op_equal(const Rational &rhs) const noexcept {
    return op_compare(rhs) == 0;
}

int Rational::op_compare(const Rational &rhs) const noexcept {
    BigNum left, right;
    mpz_mul(left.value, m_pimpl->m_numerator.value, rhs.m_pimpl->m_denominator.value);
    mpz_mul(right.value, rhs.m_pimpl->m_numerator.value, m_pimpl->m_denominator.value);
    return mpz_cmp(left.value, right.value);
}

// ============================================================================
// Free-standing operators
// ============================================================================

bool operator==(const Rational &lhs, const Rational &rhs) noexcept {
    return lhs.op_equal(rhs);
}

bool operator!=(const Rational &lhs, const Rational &rhs) noexcept {
    return lhs.op_compare(rhs) != 0;
}

bool operator<(const Rational &lhs, const Rational &rhs) noexcept {
    return lhs.op_compare(rhs) < 0;
}

bool operator<=(const Rational &lhs, const Rational &rhs) noexcept {
    return lhs.op_compare(rhs) <= 0;
}

bool operator>(const Rational &lhs, const Rational &rhs) noexcept {
    return lhs.op_compare(rhs) > 0;
}

bool operator>=(const Rational &lhs, const Rational &rhs) noexcept {
    return lhs.op_compare(rhs) >= 0;
}

Rational operator+(Rational lhs, const Rational &rhs) noexcept {
    return lhs += rhs;
}

Rational operator-(Rational lhs, const Rational &rhs) noexcept {
    return lhs -= rhs;
}

Rational operator*(Rational lhs, const Rational &rhs) noexcept {
    return lhs *= rhs;
}

Rational operator/(Rational lhs, const Rational &rhs) {
    return lhs /= rhs;
}

Rational operator+(const Rational &r) noexcept {
    return r;
}

Rational operator-(const Rational &r) noexcept {
    Rational result;
    result.set_(r);
    mpz_neg(result.m_pimpl->m_numerator.value, result.m_pimpl->m_numerator.value);
    return result;
}

} // namespace trust