// stdlib/builtin/big_integer.hpp - arbitrary-precision integers.
// Public header of the built-in BigInteger type. Header-only (inline): the GMP-backed
// implementation lives HERE, not in a compiled runtime TU, so the type is delivered
// entirely by the standard library package and needs only -lgmp at link time.
// Delivery: package file `include/stdlib/builtin/big_integer.hpp` inside share/trust/stdlib.trust
// (available via -Istdlib/include).

#pragma once

#include <compare>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <gmp.h>

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
    // Сокращённое представление для показа: "head...tail (N)", где head = tail = L цифр,
    // N - общее число десятичных цифр (без знака). Только для вывода: результат не является
    // числом и не парсится обратно. Если цифр мало (N <= 2L+1) - возвращается полное число.
    // L < 1 - исключение std::invalid_argument.
    std::string display(int64_t head_tail) const;

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
    // Свободные операторы (ниже, inline) обращаются к приватным set_()/m_pimpl
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

// Header-only implementation (previously src/runtime/big_integer.cpp)

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

inline BigInteger::BigInteger() noexcept
: m_pimpl(std::make_unique<Impl>()) {
}

inline BigInteger::BigInteger(int64_t value) noexcept
: m_pimpl(std::make_unique<Impl>(value)) {
}

inline BigInteger::BigInteger(std::string_view value)
: m_pimpl(std::make_unique<Impl>(value)) {
}

inline BigInteger::BigInteger(const BigInteger& other) noexcept
: m_pimpl(std::make_unique<Impl>()) {
    m_pimpl->SetFromString(other.GetAsString());
}

inline BigInteger::BigInteger(BigInteger&& other) noexcept = default;

inline BigInteger& BigInteger::operator=(BigInteger other) noexcept {
    m_pimpl = std::move(other.m_pimpl);
    return *this;
}

inline BigInteger::~BigInteger() noexcept = default;

inline std::string BigInteger::GetAsString() const {
    char* ptr = mpz_get_str(nullptr, 10, m_pimpl->value);
    std::string result(ptr);
    std::free(ptr);
    return result;
}

inline BigInteger::operator std::string() const {
    return GetAsString();
}

inline int64_t BigInteger::GetAsBoolean() const noexcept {
    return !m_pimpl->isZero();
}

inline int64_t BigInteger::GetAsInteger() const {
    if (m_pimpl->isOverflow()) {
        throw std::overflow_error("BigInteger integer overflow!");
    }
    return mpz_get_si(m_pimpl->value);
}

inline double BigInteger::GetAsNumber() const noexcept {
    return mpz_get_d(m_pimpl->value);
}

inline std::string BigInteger::display(int64_t head_tail) const {
    if (head_tail < 1) {
        throw std::invalid_argument("BigInteger::display: L must be >= 1");
    }
    const std::size_t l = static_cast<std::size_t>(head_tail);
    std::string s = GetAsString();
    const bool neg = !s.empty() && s.front() == '-';
    const std::string_view digits = neg ? std::string_view(s).substr(1) : std::string_view(s);
    const std::size_t n = digits.size();
    if (n <= 2 * l + 1) {
        return s; // голова+хвост покрывают число - показываем полностью
    }
    std::string out;
    out.reserve(2 * l + 24);
    if (neg) {
        out.push_back('-');
    }
    out.append(digits, 0, l);
    out += "...";
    out.append(digits, n - l, l);
    out += " (";
    out += std::to_string(n);
    out.push_back(')');
    return out;
}

inline bool BigInteger::isZero() const noexcept {
    return m_pimpl->isZero();
}

inline bool BigInteger::isOne() const noexcept {
    return m_pimpl->isOne();
}

inline bool BigInteger::isNegative() const noexcept {
    return m_pimpl->isNegative();
}

inline bool BigInteger::op_equal(const BigInteger& rhs) const noexcept {
    return op_compare(rhs) == 0;
}

inline int BigInteger::op_compare(const BigInteger& rhs) const noexcept {
    return mpz_cmp(m_pimpl->value, rhs.m_pimpl->value);
}

inline BigInteger& BigInteger::operator+=(const BigInteger& rhs) noexcept {
    mpz_add(m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return *this;
}

inline BigInteger& BigInteger::operator-=(const BigInteger& rhs) noexcept {
    mpz_sub(m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return *this;
}

inline BigInteger& BigInteger::operator*=(const BigInteger& rhs) noexcept {
    mpz_mul(m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return *this;
}

inline BigInteger& BigInteger::operator/=(const BigInteger& rhs) {
    if (rhs.m_pimpl->isZero()) {
        throw std::runtime_error("Division by zero");
    }
    mpz_tdiv_q(m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return *this;
}

inline void BigInteger::negate() noexcept {
    mpz_neg(m_pimpl->value, m_pimpl->value);
}

inline std::pair<BigInteger, BigInteger> BigInteger::divmod(const BigInteger& rhs) const {
    if (rhs.m_pimpl->isZero()) {
        throw std::runtime_error("Division by zero");
    }
    BigInteger q, r;
    mpz_fdiv_qr(q.m_pimpl->value, r.m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return {std::move(q), std::move(r)};
}

inline BigInteger BigInteger::divideExact(const BigInteger& rhs) const {
    if (rhs.m_pimpl->isZero()) {
        throw std::runtime_error("Division by zero");
    }
    BigInteger result;
    mpz_divexact(result.m_pimpl->value, m_pimpl->value, rhs.m_pimpl->value);
    return result;
}

inline BigInteger BigInteger::gcd(const BigInteger& a, const BigInteger& b) noexcept {
    BigInteger result;
    mpz_gcd(result.m_pimpl->value, a.m_pimpl->value, b.m_pimpl->value);
    return result;
}

inline BigInteger& BigInteger::set_(int64_t value) noexcept {
    *m_pimpl = Impl(value);
    return *this;
}

inline BigInteger& BigInteger::set_(const BigInteger& copy) noexcept {
    m_pimpl->SetFromString(copy.GetAsString());
    return *this;
}

inline BigInteger& BigInteger::set_(std::string_view value) {
    m_pimpl->SetFromString(value);
    return *this;
}

// Free operators. Сравнение реализовано через operator== и operator<=> (C++20): из них
// компилятор синтезирует != (из ==) и <, <=, >, >= (из <=>) - без копипаста шести операторов.
inline bool operator==(const BigInteger& lhs, const BigInteger& rhs) noexcept {
    return lhs.op_equal(rhs);
}

inline std::strong_ordering operator<=>(const BigInteger& lhs, const BigInteger& rhs) noexcept {
    const int c = lhs.op_compare(rhs);
    return c < 0 ? std::strong_ordering::less : (c > 0 ? std::strong_ordering::greater : std::strong_ordering::equal);
}

inline BigInteger operator+(BigInteger lhs, const BigInteger& rhs) noexcept {
    return lhs += rhs;
}

inline BigInteger operator-(BigInteger lhs, const BigInteger& rhs) noexcept {
    return lhs -= rhs;
}

inline BigInteger operator*(BigInteger lhs, const BigInteger& rhs) noexcept {
    return lhs *= rhs;
}

inline BigInteger operator/(BigInteger lhs, const BigInteger& rhs) {
    return lhs /= rhs;
}

inline BigInteger operator+(const BigInteger& r) noexcept {
    return r;
}

inline BigInteger operator-(const BigInteger& r) noexcept {
    BigInteger result;
    result.set_(r);
    result.negate();
    return result;
}

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
