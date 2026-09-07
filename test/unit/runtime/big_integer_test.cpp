// Test file: BigInteger runtime type.
#include "trust/big_integer.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <stdexcept>

using trust::BigInteger;

namespace {

// GetAsString: decimal representation.
TEST(BigIntegerTest, DefaultCtor) {
    BigInteger b;
    EXPECT_EQ(b.GetAsString(), "0");
    EXPECT_FALSE(b.GetAsBoolean()); // 0 -> false
    EXPECT_TRUE(b.isZero());
}

TEST(BigIntegerTest, IntCtor) {
    BigInteger b(42);
    EXPECT_EQ(b.GetAsString(), "42");
    EXPECT_TRUE(b.GetAsBoolean());
    EXPECT_EQ(b.GetAsInteger(), 42);
    BigInteger neg(-7);
    EXPECT_EQ(neg.GetAsString(), "-7");
    EXPECT_TRUE(neg.isNegative());
}

TEST(BigIntegerTest, StringCtor) {
    EXPECT_EQ(BigInteger("0").GetAsString(), "0");
    EXPECT_EQ(BigInteger("123").GetAsString(), "123");
    EXPECT_EQ(BigInteger("-55").GetAsString(), "-55");
    // Сверхразрядное значение (вне Int64) хранится точно.
    EXPECT_EQ(BigInteger("9223372036854775808").GetAsString(), "9223372036854775808");
    EXPECT_EQ(BigInteger("-9223372036854775808").GetAsString(), "-9223372036854775808");
    // Нечисловая строка - ошибка.
    EXPECT_THROW(BigInteger("abc"), std::runtime_error);
}

TEST(BigIntegerTest, HugeArithmetic) {
    // Умножение значений за пределами Int64.
    BigInteger a("12345678901234567890");
    BigInteger b("98765432109876543210");
    BigInteger p = a * b;
    EXPECT_EQ(p.GetAsString(), "1219326311370217952237463801111263526900");
    BigInteger sum = a + b;
    EXPECT_EQ(sum.GetAsString(), "111111111011111111100");
}

TEST(BigIntegerTest, Comparison) {
    EXPECT_TRUE(BigInteger(1) < BigInteger(2));
    EXPECT_TRUE(BigInteger(2) > BigInteger(1));
    EXPECT_TRUE(BigInteger(1) == BigInteger(1));
    EXPECT_TRUE(BigInteger(1) != BigInteger(2));
    EXPECT_TRUE(BigInteger(1) <= BigInteger(1));
    EXPECT_TRUE(BigInteger(2) >= BigInteger(1));
    EXPECT_TRUE(BigInteger("9223372036854775808") > BigInteger(9223372036854775807LL));
}

TEST(BigIntegerTest, Arithmetic) {
    EXPECT_EQ((BigInteger(7) + BigInteger(3)).GetAsString(), "10");
    EXPECT_EQ((BigInteger(7) - BigInteger(3)).GetAsString(), "4");
    EXPECT_EQ((BigInteger(7) * BigInteger(3)).GetAsString(), "21");
    EXPECT_EQ((BigInteger(7) / BigInteger(3)).GetAsString(), "2"); // truncation
    EXPECT_EQ((BigInteger(-7) / BigInteger(3)).GetAsString(), "-2");
    EXPECT_EQ((-BigInteger(5)).GetAsString(), "-5");
    EXPECT_EQ((+BigInteger(5)).GetAsString(), "5");
}

TEST(BigIntegerTest, DivmodFloor) {
    auto [q, r] = BigInteger(7).divmod(BigInteger(3));
    EXPECT_EQ(q.GetAsString(), "2");
    EXPECT_EQ(r.GetAsString(), "1");
    // floor: отрицательное делимое округляется вниз.
    auto [qn, rn] = BigInteger(-7).divmod(BigInteger(3));
    EXPECT_EQ(qn.GetAsString(), "-3");
    EXPECT_EQ(rn.GetAsString(), "2");
}

TEST(BigIntegerTest, DivideExactAndGcd) {
    BigInteger g = BigInteger::gcd(BigInteger(12), BigInteger(18));
    EXPECT_EQ(g.GetAsString(), "6");
    EXPECT_EQ(BigInteger(12).divideExact(BigInteger(6)).GetAsString(), "2");
}

TEST(BigIntegerTest, Conversions) {
    BigInteger b(42);
    EXPECT_EQ(b.GetAsInteger(), 42);
    EXPECT_DOUBLE_EQ(BigInteger(7).GetAsNumber(), 7.0);
    // Переполнение при приведении к int64.
    EXPECT_THROW(BigInteger("9223372036854775808").GetAsInteger(), std::overflow_error);
    EXPECT_EQ(std::string(BigInteger(42)), "42");
}

TEST(BigIntegerTest, ThrowsOnDivisionByZero) {
    EXPECT_THROW(BigInteger(1) / BigInteger(0), std::runtime_error);
    EXPECT_THROW(BigInteger(1).divmod(BigInteger(0)), std::runtime_error);
}

TEST(BigIntegerTest, CopyAndAssign) {
    BigInteger a("12345678901234567890");
    BigInteger b(a); // copy ctor
    EXPECT_EQ(b.GetAsString(), "12345678901234567890");
    BigInteger c;
    c = a; // copy assignment
    EXPECT_EQ(c.GetAsString(), "12345678901234567890");
    c = BigInteger(7); // move assignment
    EXPECT_EQ(c.GetAsString(), "7");
}

} // namespace
