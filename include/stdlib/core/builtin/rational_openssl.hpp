#pragma once
// builtin/rational_openssl.hpp - example ALTERNATIVE implementation for built-in
// Rational/BigInteger, implemented with OpenSSL BIGNUM (libcrypto). Delivered as
// @stdlib/builtin/rational_openssl.hpp; the corresponding .src defines the built-in types
// via `=` and links -lcrypto. The default implementation remains stdlib/builtin/rational.hpp.

#include <openssl/bn.h>
#include <openssl/crypto.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace openssl {

class BigInteger {
  public:
    BigInteger()
    : m_bn(BN_new()) {
        if (!m_bn) {
            throw std::bad_alloc();
        }
    }
    BigInteger(int64_t value)
    : BigInteger() {
        const uint64_t mag = value < 0 ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);
        if (BN_set_word(m_bn, mag) != 1) {
            throw std::runtime_error("openssl BigInteger: BN_set_word failed");
        }
        BN_set_negative(m_bn, value < 0);
    }
    explicit BigInteger(std::string_view value)
    : BigInteger() {
        const std::string s(value);
        const int used = BN_dec2bn(&m_bn, s.c_str());
        if (used == 0 || static_cast<std::size_t>(used) != s.size()) {
            throw std::invalid_argument("openssl BigInteger: invalid decimal string");
        }
    }
    BigInteger(const BigInteger& other)
    : m_bn(BN_dup(other.m_bn)) {
        if (!m_bn) {
            throw std::bad_alloc();
        }
    }
    BigInteger(BigInteger&& other) noexcept
    : m_bn(std::exchange(other.m_bn, nullptr)) {}
    BigInteger& operator=(BigInteger other) noexcept {
        std::swap(m_bn, other.m_bn);
        return *this;
    }
    ~BigInteger() { BN_free(m_bn); }

    std::string GetAsString() const {
        char* p = BN_bn2dec(m_bn);
        if (!p) {
            throw std::runtime_error("openssl BigInteger: BN_bn2dec failed");
        }
        std::string s(p);
        OPENSSL_free(p);
        return s;
    }
    operator std::string() const { return GetAsString(); }
    int64_t GetAsBoolean() const noexcept { return !BN_is_zero(m_bn); }
    int64_t GetAsInteger() const {
        try {
            return std::stoll(GetAsString());
        } catch (const std::out_of_range&) {
            throw std::overflow_error("openssl BigInteger: value does not fit int64_t");
        }
    }
    double GetAsNumber() const noexcept {
        try {
            return std::stod(GetAsString());
        } catch (const std::out_of_range&) {
            return isNegative() ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        }
    }
    std::string display(int64_t head_tail) const {
        if (head_tail < 1) {
            throw std::invalid_argument("openssl BigInteger::display: head_tail must be >= 1");
        }
        std::string s = GetAsString();
        const bool neg = !s.empty() && s.front() == '-';
        const std::string digits = neg ? s.substr(1) : s;
        const std::size_t n = digits.size();
        const std::size_t l = static_cast<std::size_t>(head_tail);
        if (n <= 2 * l + 1) {
            return s;
        }
        return (neg ? "-" : "") + digits.substr(0, l) + "..." + digits.substr(n - l) + " (" + std::to_string(n) + ")";
    }
    bool isZero() const noexcept { return BN_is_zero(m_bn); }
    bool isOne() const noexcept { return BN_is_one(m_bn); }
    bool isNegative() const noexcept { return BN_is_negative(m_bn); }
    bool op_equal(const BigInteger& rhs) const noexcept { return BN_cmp(m_bn, rhs.m_bn) == 0; }
    int op_compare(const BigInteger& rhs) const { return BN_cmp(m_bn, rhs.m_bn); }

    BigInteger& operator+=(const BigInteger& rhs) {
        if (BN_add(m_bn, m_bn, rhs.m_bn) != 1) {
            throw std::runtime_error("openssl BigInteger: BN_add failed");
        }
        return *this;
    }
    BigInteger& operator-=(const BigInteger& rhs) {
        if (BN_sub(m_bn, m_bn, rhs.m_bn) != 1) {
            throw std::runtime_error("openssl BigInteger: BN_sub failed");
        }
        return *this;
    }
    BigInteger& operator*=(const BigInteger& rhs) {
        BN_CTX* ctx = BN_CTX_new();
        if (!ctx || BN_mul(m_bn, m_bn, rhs.m_bn, ctx) != 1) {
            BN_CTX_free(ctx);
            throw std::runtime_error("openssl BigInteger: BN_mul failed");
        }
        BN_CTX_free(ctx);
        return *this;
    }
    BigInteger& operator/=(const BigInteger& rhs) {
        if (rhs.isZero()) {
            throw std::runtime_error("openssl BigInteger: division by zero");
        }
        if (this == &rhs) {
            *this = BigInteger(1);
            return *this;
        }
        BN_CTX* ctx = BN_CTX_new();
        BIGNUM* q = BN_new();
        if (!ctx || !q || BN_div(q, nullptr, m_bn, rhs.m_bn, ctx) != 1) {
            BN_free(q);
            BN_CTX_free(ctx);
            throw std::runtime_error("openssl BigInteger: BN_div failed");
        }
        BN_free(m_bn);
        m_bn = q;
        BN_CTX_free(ctx);
        return *this;
    }
    void negate() noexcept { BN_set_negative(m_bn, !BN_is_negative(m_bn)); }

    std::pair<BigInteger, BigInteger> divmod(const BigInteger& rhs) const {
        if (rhs.isZero()) {
            throw std::runtime_error("openssl BigInteger: division by zero");
        }
        if (this == &rhs) {
            return {BigInteger(1), BigInteger(0)};
        }
        BN_CTX* ctx = BN_CTX_new();
        BIGNUM* q = BN_new();
        BIGNUM* r = BN_new();
        if (!ctx || !q || !r || BN_div(q, r, m_bn, rhs.m_bn, ctx) != 1) {
            BN_free(q);
            BN_free(r);
            BN_CTX_free(ctx);
            throw std::runtime_error("openssl BigInteger: BN_div failed");
        }
        BigInteger outQ;
        BigInteger outR;
        BN_free(outQ.m_bn);
        BN_free(outR.m_bn);
        outQ.m_bn = q;
        outR.m_bn = r;
        BN_CTX_free(ctx);
        // BN_div truncates toward zero; the runtime contract (mpz_fdiv_qr) floors.
        if (!outR.isZero() && (outR.isNegative() != rhs.isNegative())) {
            outQ -= BigInteger(1);
            outR += rhs;
        }
        return {std::move(outQ), std::move(outR)};
    }
    BigInteger divideExact(const BigInteger& rhs) const {
        auto [q, r] = divmod(rhs);
        if (!r.isZero()) {
            throw std::domain_error("openssl BigInteger: not divisible");
        }
        return q;
    }
    static BigInteger gcd(const BigInteger& a, const BigInteger& b) {
        BigInteger out;
        BN_CTX* ctx = BN_CTX_new();
        if (!ctx || BN_gcd(out.m_bn, a.m_bn, b.m_bn, ctx) != 1) {
            BN_CTX_free(ctx);
            throw std::runtime_error("openssl BigInteger: BN_gcd failed");
        }
        BN_CTX_free(ctx);
        return out;
    }

  private:
    BIGNUM* m_bn;
};

inline BigInteger operator+(BigInteger lhs, const BigInteger& rhs) {
    lhs += rhs;
    return lhs;
}
inline BigInteger operator-(BigInteger lhs, const BigInteger& rhs) {
    lhs -= rhs;
    return lhs;
}
inline BigInteger operator*(BigInteger lhs, const BigInteger& rhs) {
    lhs *= rhs;
    return lhs;
}
inline BigInteger operator/(BigInteger lhs, const BigInteger& rhs) {
    lhs /= rhs;
    return lhs;
}
inline BigInteger operator+(const BigInteger& r) {
    return r;
}
inline BigInteger operator-(const BigInteger& r) {
    BigInteger out = r;
    out.negate();
    return out;
}
inline bool operator==(const BigInteger& lhs, const BigInteger& rhs) noexcept {
    return lhs.op_equal(rhs);
}
inline std::strong_ordering operator<=>(const BigInteger& lhs, const BigInteger& rhs) noexcept {
    const int c = lhs.op_compare(rhs);
    return c < 0 ? std::strong_ordering::less : (c > 0 ? std::strong_ordering::greater : std::strong_ordering::equal);
}
inline BigInteger abs(const BigInteger& v) {
    BigInteger out = v;
    if (out.isNegative()) {
        out.negate();
    }
    return out;
}
class Rational {
  public:
    Rational()
    : m_num(0)
    , m_den(1) {}
    Rational(int64_t value)
    : m_num(value)
    , m_den(1) {}
    Rational(const BigInteger& value)
    : m_num(value)
    , m_den(1) {}
    Rational(BigInteger num, BigInteger den)
    : m_num(std::move(num))
    , m_den(std::move(den)) {
        normalize();
    }
    Rational(std::string_view numerator, std::string_view denominator)
    : m_num(numerator)
    , m_den(denominator) {
        normalize();
    }
    explicit Rational(std::string_view value) {
        const auto sep = value.find('\\');
        if (sep == std::string_view::npos) {
            throw std::runtime_error("openssl Rational: string must be in form 'num\\den'");
        }
        m_num = BigInteger(value.substr(0, sep));
        m_den = BigInteger(value.substr(sep + 1));
        normalize();
    }
    Rational(const Rational&) = default;
    Rational(Rational&&) noexcept = default;
    Rational& operator=(Rational other) noexcept {
        std::swap(m_num, other.m_num);
        std::swap(m_den, other.m_den);
        return *this;
    }
    ~Rational() = default;

    std::string GetAsString() const { return m_num.GetAsString() + "\\" + m_den.GetAsString(); }
    operator std::string() const { return GetAsString(); }
    int64_t GetAsBoolean() const noexcept { return !m_num.isZero(); }
    int64_t GetAsInteger() const { return m_num.divmod(m_den).first.GetAsInteger(); }
    double GetAsNumber() const { return m_num.GetAsNumber() / m_den.GetAsNumber(); }
    std::string display(int64_t head_tail) const { return m_num.display(head_tail) + "\\" + m_den.display(head_tail); }

    bool isInteger() const noexcept { return m_den.isOne(); }
    Rational reciprocal() const {
        if (m_num.isZero()) {
            throw std::runtime_error("openssl Rational: reciprocal of zero");
        }
        return Rational(m_den, m_num);
    }
    static Rational abs(const Rational& r) {
        Rational out = r;
        if (out.m_num.isNegative()) {
            out.m_num.negate();
        }
        return out;
    }

    Rational& operator*=(const Rational& rhs) {
        m_num *= rhs.m_num;
        m_den *= rhs.m_den;
        normalize();
        return *this;
    }
    Rational& operator/=(const Rational& rhs) {
        if (rhs.m_num.isZero()) {
            throw std::runtime_error("openssl Rational: division by zero");
        }
        // Locals make the operation self-assignment safe (`r /= r`).
        BigInteger num = m_num * rhs.m_den;
        BigInteger den = m_den * rhs.m_num;
        m_num = std::move(num);
        m_den = std::move(den);
        normalize();
        return *this;
    }
    Rational& operator+=(const Rational& rhs) {
        m_num = m_num * rhs.m_den + rhs.m_num * m_den;
        m_den *= rhs.m_den;
        normalize();
        return *this;
    }
    Rational& operator-=(const Rational& rhs) {
        m_num = m_num * rhs.m_den - rhs.m_num * m_den;
        m_den *= rhs.m_den;
        normalize();
        return *this;
    }

    bool op_equal(const Rational& rhs) const noexcept { return m_num.op_equal(rhs.m_num) && m_den.op_equal(rhs.m_den); }
    int op_compare(const Rational& rhs) const { return (m_num * rhs.m_den).op_compare(rhs.m_num * m_den); }

  private:
    BigInteger m_num;
    BigInteger m_den;

    void normalize() {
        if (m_den.isZero()) {
            throw std::runtime_error("openssl Rational: zero denominator");
        }
        if (m_den.isNegative()) {
            m_den.negate();
            m_num.negate();
        }
        const BigInteger g = BigInteger::gcd(openssl::abs(m_num), m_den);
        if (!g.isOne() && !g.isZero()) {
            m_num = m_num.divideExact(g);
            m_den = m_den.divideExact(g);
        }
    }
};

inline Rational operator+(Rational lhs, const Rational& rhs) {
    lhs += rhs;
    return lhs;
}
inline Rational operator-(Rational lhs, const Rational& rhs) {
    lhs -= rhs;
    return lhs;
}
inline Rational operator*(Rational lhs, const Rational& rhs) {
    lhs *= rhs;
    return lhs;
}
inline Rational operator/(Rational lhs, const Rational& rhs) {
    lhs /= rhs;
    return lhs;
}
inline Rational operator+(const Rational& r) {
    return r;
}
inline Rational operator-(const Rational& r) {
    return Rational(BigInteger(0)) - r;
}
inline bool operator==(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.op_equal(rhs);
}
inline std::strong_ordering operator<=>(const Rational& lhs, const Rational& rhs) {
    const int c = lhs.op_compare(rhs);
    return c < 0 ? std::strong_ordering::less : (c > 0 ? std::strong_ordering::greater : std::strong_ordering::equal);
}

} // namespace openssl

namespace openssl::detail {

// The runtime formatters reject precision for arbitrary-precision values: a
// precision spec would silently truncate the decimal/symbolic string.
template <typename T>
struct StringFormatter : std::formatter<std::string> {
    constexpr auto parse(std::format_parse_context& ctx) {
        auto it = ctx.begin();
        for (auto p = it; p != ctx.end() && *p != '}'; ++p) {
            if (*p == '.') {
                const bool is_fill = (p == it) && (p + 1) != ctx.end() && (*(p + 1) == '<' || *(p + 1) == '>' || *(p + 1) == '^');
                if (!is_fill) {
                    throw std::format_error("precision is not supported for openssl provider values");
                }
            }
        }
        return std::formatter<std::string>::parse(ctx);
    }
    template <typename FormatContext>
    auto format(const T& v, FormatContext& ctx) const {
        return std::formatter<std::string>::format(v.GetAsString(), ctx);
    }
};

} // namespace openssl::detail

template <>
struct std::formatter<openssl::BigInteger> : openssl::detail::StringFormatter<openssl::BigInteger> {};

template <>
struct std::formatter<openssl::Rational> : openssl::detail::StringFormatter<openssl::Rational> {};
