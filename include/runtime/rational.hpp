#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace trust {

class Rational {
  public:
    Rational() noexcept;
    ~Rational() noexcept;

    explicit Rational(int64_t value) noexcept;
    Rational(std::string_view numerator, std::string_view denominator);

    Rational(const Rational &other) noexcept;
    Rational(Rational &&other) noexcept;

    Rational &operator=(Rational other) noexcept;

    std::string GetAsString() const;
    int64_t GetAsBoolean() const noexcept;
    int64_t GetAsInteger() const;
    double GetAsNumber() const;

    void reduce() noexcept;

    Rational &set_(int64_t value) noexcept;
    Rational &set_(const Rational &copy) noexcept;
    Rational &set_(std::string_view numerator, std::string_view denominator);

    void NormalizeSign() noexcept;

    Rational &operator*=(const Rational &rhs) noexcept;
    Rational &operator/=(const Rational &rhs);
    Rational &operator+=(const Rational &rhs) noexcept;
    Rational &operator-=(const Rational &rhs) noexcept;

    bool op_equal(const Rational &rhs) const noexcept;
    int op_compare(const Rational &rhs) const noexcept;

    bool isInteger() const noexcept;
    Rational reciprocal() const;
    static Rational abs(const Rational &r) noexcept;

    friend Rational operator+(const Rational &r) noexcept;
    friend Rational operator-(const Rational &r) noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> m_pimpl;
};

// Standard comparison operators
bool operator==(const Rational &lhs, const Rational &rhs) noexcept;
bool operator!=(const Rational &lhs, const Rational &rhs) noexcept;
bool operator<(const Rational &lhs, const Rational &rhs) noexcept;
bool operator<=(const Rational &lhs, const Rational &rhs) noexcept;
bool operator>(const Rational &lhs, const Rational &rhs) noexcept;
bool operator>=(const Rational &lhs, const Rational &rhs) noexcept;

// Binary operators for Rational
Rational operator+(Rational lhs, const Rational &rhs) noexcept;
Rational operator-(Rational lhs, const Rational &rhs) noexcept;
Rational operator*(Rational lhs, const Rational &rhs) noexcept;
Rational operator/(Rational lhs, const Rational &rhs);

} // namespace trust