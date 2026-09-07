// trust/big_integer.cpp - arbitrary-precision integer (GMP-backed).
// Public runtime implementation; linked into trust-runtime.so/.a.

#include "trust/big_integer.hpp"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <gmp.h>

namespace trust {

// ============================================================================
// BigInteger::Impl definition
// ============================================================================

class BigInteger::Impl {
  public:
    mpz_t value;

    Impl() noexcept { mpz_init(value); }
    ~Impl() noexcept { mpz_clear(value); }

    explicit Impl(int64_t var) noexcept {
        mpz_init(value);
        mpz_set_si(value, var);
    }

    explicit Impl(std::string_view str) {
        mpz_init(value);
        SetFromString(str);
    }

    Impl(const Impl& other) { mpz_init_set(value, other.value); }

    Impl(Impl&& other) noexcept {
        mpz_init(value);
        mpz_swap(value, other.value);
    }

    Impl& operator=(Impl other) noexcept {
        mpz_swap(value, other.value);
        return *this;
    }

    Impl& operator=(int64_t var) noexcept {
        mpz_set_si(value, var);
        return *this;
    }

    bool SetFromString(std::string_view str) {
        if (str.empty()) {
            throw std::invalid_argument("String cannot be empty");
        }
        std::string temp(str);
        int result = mpz_set_str(value, temp.c_str(), 10);
        if (result != 0) {
            throw std::runtime_error("Failed to create BigInteger from string '" + std::string(str) + "'");
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
// BigInteger implementation
// ============================================================================

BigInteger::BigInteger() noexcept
: m_pimpl(std::make_unique<Impl>()) {
}

BigInteger::BigInteger(int64_t value) noexcept
: m_pimpl(std::make_unique<Impl>(value)) {
}

BigInteger::BigInteger(std::string_view value)
: m_pimpl(std::make_unique<Impl>(value)) {
}

BigInteger::BigInteger(const BigInteger& other) noexcept
: m_pimpl(std::make_unique<Impl>()) {
    m_pimpl->SetFromString(other.GetAsString());
}

BigInteger::BigInteger(BigInteger&& other) noexcept = default;

BigInteger& BigInteger::operator=(BigInteger other) noexcept {
    m_pimpl = std::move(other.m_pimpl);
    return *this;
}

BigInteger::~BigInteger() noexcept = default;

std::string BigInteger::GetAsString() const {
    char* ptr = mpz_get_str(nullptr, 10, m_pimpl->value);
    std::string result(ptr);
    std::free(ptr);
    return result;
}

BigInteger::operator std::string() const {
    return GetAsString();
}

int64_t BigInteger::GetAsBoolean() const noexcept {
    return !m_pimpl->isZero();
}

int64_t BigInteger::GetAsInteger() const {
    if (m_pimpl->isOverflow()) {
        throw std::overflow_error("BigInteger integer overflow!");
    }
    return mpz_get_si(m_pimpl->value);
}

double BigInteger::GetAsNumber() const noexcept {
    return mpz_get_d(m_pimpl->value);
}

bool BigInteger::isZero() const noexcept {
    return m_pimpl->isZero();
}

bool BigInteger::isOne() const noexcept {
    return m_pimpl->isOne();
}

bool BigInteger::isNegative() const noexcept {
    return m_pimpl->isNegative();
}

bool BigInteger::op_equal(const BigInteger& rhs) const noexcept {
    return op_compare(rhs) == 0;
}

int BigInteger::op_compare(const BigInteger& rhs) const noexcept {
    return mpz_cmp(m_pimpl->value, rhs.m_pimpl->value);
}

BigInteger& BigInteger::operator+=(const BigInteger& rhs) noexcept {
    mpz_add(m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return *this;
}

BigInteger& BigInteger::operator-=(const BigInteger& rhs) noexcept {
    mpz_sub(m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return *this;
}

BigInteger& BigInteger::operator*=(const BigInteger& rhs) noexcept {
    mpz_mul(m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return *this;
}

BigInteger& BigInteger::operator/=(const BigInteger& rhs) {
    if (rhs.m_pimpl->isZero()) {
        throw std::runtime_error("Division by zero");
    }
    mpz_tdiv_q(m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return *this;
}

void BigInteger::negate() noexcept {
    mpz_neg(m_pimpl->value, m_pimpl->value);
}

std::pair<BigInteger, BigInteger> BigInteger::divmod(const BigInteger& rhs) const {
    if (rhs.m_pimpl->isZero()) {
        throw std::runtime_error("Division by zero");
    }
    BigInteger q, r;
    mpz_fdiv_qr(q.m_pimpl->value, r.m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return {std::move(q), std::move(r)};
}

BigInteger BigInteger::divideExact(const BigInteger& rhs) const {
    if (rhs.m_pimpl->isZero()) {
        throw std::runtime_error("Division by zero");
    }
    BigInteger result;
    mpz_divexact(result.m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return result;
}

BigInteger BigInteger::gcd(const BigInteger& a, const BigInteger& b) noexcept {
    BigInteger result;
    mpz_gcd(result.m_pimpl->value, a.m_pimpl->value, b.m_pimpl->value);
    return result;
}

BigInteger& BigInteger::set_(int64_t value) noexcept {
    *m_pimpl = Impl(value);
    return *this;
}

BigInteger& BigInteger::set_(const BigInteger& copy) noexcept {
    m_pimpl->SetFromString(copy.GetAsString());
    return *this;
}

BigInteger& BigInteger::set_(std::string_view value) {
    m_pimpl->SetFromString(value);
    return *this;
}

// ============================================================================
// Free-standing operators
// ============================================================================

bool operator==(const BigInteger& lhs, const BigInteger& rhs) noexcept {
    return lhs.op_equal(rhs);
}

// C++20: из operator<=> компилятор синтезирует !=, <, <=, >, >= (а из == - !=),
// поэтому отдельные определения шести операторов не нужны.
std::strong_ordering operator<=>(const BigInteger& lhs, const BigInteger& rhs) noexcept {
    const int c = lhs.op_compare(rhs);
    return c < 0 ? std::strong_ordering::less : (c > 0 ? std::strong_ordering::greater : std::strong_ordering::equal);
}

BigInteger operator+(BigInteger lhs, const BigInteger& rhs) noexcept {
    return lhs += rhs;
}

BigInteger operator-(BigInteger lhs, const BigInteger& rhs) noexcept {
    return lhs -= rhs;
}

BigInteger operator*(BigInteger lhs, const BigInteger& rhs) noexcept {
    return lhs *= rhs;
}

BigInteger operator/(BigInteger lhs, const BigInteger& rhs) {
    return lhs /= rhs;
}

BigInteger operator+(const BigInteger& r) noexcept {
    return r;
}

BigInteger operator-(const BigInteger& r) noexcept {
    BigInteger result;
    result.set_(r);
    result.negate();
    return result;
}

} // namespace trust
