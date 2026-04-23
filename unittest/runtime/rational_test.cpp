#include "runtime/rational.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace trust;

// ============================================================================
// Rational Tests
// ============================================================================

class RationalTest : public ::testing::Test {};

TEST_F(RationalTest, DefaultConstructor) {
    Rational r;
    EXPECT_EQ(r.GetAsInteger(), 0);
    EXPECT_EQ(r.GetAsBoolean(), 0);
}

TEST_F(RationalTest, ConstructorFromInt64) {
    Rational r(42);
    EXPECT_EQ(r.GetAsInteger(), 42);
    EXPECT_EQ(r.GetAsBoolean(), 1);
}

TEST_F(RationalTest, ConstructorFromInt64Zero) {
    Rational r(0);
    EXPECT_EQ(r.GetAsInteger(), 0);
    EXPECT_EQ(r.GetAsBoolean(), 0);
}

TEST_F(RationalTest, ConstructorFromInt64Negative) {
    Rational r(-5);
    EXPECT_EQ(r.GetAsInteger(), -5);
    EXPECT_EQ(r.GetAsBoolean(), 1);
}

TEST_F(RationalTest, ConstructorFromString) {
    Rational r("3", "4");
    EXPECT_EQ(r.GetAsString(), "3\\4");
}

TEST_F(RationalTest, ConstructorFromStringAutoReduce) {
    Rational r("6", "8");
    EXPECT_EQ(r.GetAsString(), "3\\4");
}

TEST_F(RationalTest, ConstructorFromStringDenominatorZero) {
    EXPECT_THROW(Rational("1", "0"), std::runtime_error);
}

TEST_F(RationalTest, ConstructorFromStringNormalizeSign) {
    Rational r("3", "-4");
    EXPECT_EQ(r.GetAsString(), "-3\\4");
}

TEST_F(RationalTest, ConstructorFromStringBothNegative) {
    Rational r("-3", "-4");
    EXPECT_EQ(r.GetAsString(), "3\\4");
}

TEST_F(RationalTest, GetAsInteger) {
    Rational r("7", "2");
    EXPECT_EQ(r.GetAsInteger(), 3);
}

TEST_F(RationalTest, GetAsIntegerExact) {
    Rational r("8", "2");
    EXPECT_EQ(r.GetAsInteger(), 4);
}

TEST_F(RationalTest, GetAsNumber) {
    Rational r("1", "2");
    EXPECT_NEAR(r.GetAsNumber(), 0.5, 1e-10);
}

TEST_F(RationalTest, GetAsString) {
    Rational r("5", "7");
    EXPECT_EQ(r.GetAsString(), "5\\7");
}

TEST_F(RationalTest, SetInt64) {
    Rational r("3", "4");
    r.set_(42);
    EXPECT_EQ(r.GetAsInteger(), 42);
    EXPECT_EQ(r.GetAsString(), "42\\1");
}

TEST_F(RationalTest, SetCopy) {
    Rational a("3", "4");
    Rational b;
    b.set_(a);
    EXPECT_EQ(a, b);
}

TEST_F(RationalTest, SetFromString) {
    Rational r;
    r.set_("5", "6");
    EXPECT_EQ(r.GetAsString(), "5\\6");
}

TEST_F(RationalTest, Add) {
    Rational a("1", "2");
    Rational b("1", "3");
    Rational c = a + b;
    EXPECT_EQ(c.GetAsString(), "5\\6");
}

TEST_F(RationalTest, Sub) {
    Rational a("1", "2");
    Rational b("1", "3");
    Rational c = a - b;
    EXPECT_EQ(c.GetAsString(), "1\\6");
}

TEST_F(RationalTest, Mul) {
    Rational a("2", "3");
    Rational b("3", "4");
    Rational c = a * b;
    EXPECT_EQ(c.GetAsString(), "1\\2");
}

TEST_F(RationalTest, Div) {
    Rational a("2", "3");
    Rational b("4", "5");
    Rational c = a / b;
    EXPECT_EQ(c.GetAsString(), "5\\6");
}

TEST_F(RationalTest, DivByZero) {
    Rational a("1", "2");
    Rational b("0", "1");
    EXPECT_THROW(a / b, std::runtime_error);
}

TEST_F(RationalTest, AdditionAssociative) {
    Rational a("1", "2");
    Rational b("1", "3");
    Rational c("1", "4");
    Rational left = (a + b) + c;
    Rational right = a + (b + c);
    EXPECT_EQ(left, right);
}

TEST_F(RationalTest, AdditionCommutative) {
    Rational a("2", "5");
    Rational b("3", "7");
    EXPECT_EQ(a + b, b + a);
}

TEST_F(RationalTest, MultiplicationCommutative) {
    Rational a("3", "4");
    Rational b("5", "6");
    EXPECT_EQ(a * b, b * a);
}

TEST_F(RationalTest, MulIdentity) {
    Rational a("7", "8");
    Rational one(1);
    EXPECT_EQ(a * one, a);
}

TEST_F(RationalTest, AddIdentity) {
    Rational a("3", "5");
    Rational zero(0);
    EXPECT_EQ(a + zero, a);
}

TEST_F(RationalTest, CompoundAssignment) {
    Rational a("1", "2");
    Rational b("1", "4");
    a += b;
    EXPECT_EQ(a.GetAsString(), "3\\4");
}

TEST_F(RationalTest, CompoundSubAssignment) {
    Rational a("3", "4");
    Rational b("1", "4");
    a -= b;
    EXPECT_EQ(a.GetAsString(), "1\\2");
}

TEST_F(RationalTest, CompoundMulAssignment) {
    Rational a("2", "3");
    Rational b("3", "4");
    a *= b;
    EXPECT_EQ(a.GetAsString(), "1\\2");
}

TEST_F(RationalTest, CompoundDivAssignment) {
    Rational a("2", "3");
    Rational b("4", "5");
    a /= b;
    EXPECT_EQ(a.GetAsString(), "5\\6");
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST_F(RationalTest, Equal) {
    Rational a("1", "2");
    Rational b("2", "4");
    EXPECT_EQ(a, b);
}

TEST_F(RationalTest, NotEqual) {
    Rational a("1", "2");
    Rational b("1", "3");
    EXPECT_NE(a, b);
}

TEST_F(RationalTest, LessThan) {
    Rational a("1", "3");
    Rational b("1", "2");
    EXPECT_LT(a, b);
}

TEST_F(RationalTest, LessThanOrEqual) {
    Rational a("1", "2");
    Rational b("1", "2");
    Rational c("1", "3");
    EXPECT_LE(a, b);
    EXPECT_LE(c, a);
}

TEST_F(RationalTest, GreaterThan) {
    Rational a("1", "2");
    Rational b("1", "3");
    EXPECT_GT(a, b);
}

TEST_F(RationalTest, GreaterThanOrEqual) {
    Rational a("1", "2");
    Rational b("1", "2");
    Rational c("1", "3");
    EXPECT_GE(a, b);
    EXPECT_GE(a, c);
}

TEST_F(RationalTest, NegativeComparison) {
    Rational a("-1", "2");
    Rational b("-1", "3");
    EXPECT_LT(a, b);
}

TEST_F(RationalTest, NegativeVsPositive) {
    Rational a("-1", "2");
    Rational b("1", "2");
    EXPECT_LT(a, b);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(RationalTest, LargeNumbers) {
    Rational a("12345678901234567890", "1");
    Rational b("98765432109876543210", "1");
    Rational c = a + b;
    EXPECT_EQ(c.GetAsString(), "111111111011111111100\\1");
}

TEST_F(RationalTest, LargeNumberReduction) {
    Rational a("12345678901234567890", "123456789012345678900");
    EXPECT_EQ(a.GetAsString(), "1\\10");
}

TEST_F(RationalTest, IntegerRational) {
    Rational r("100", "1");
    EXPECT_EQ(r.GetAsInteger(), 100);
    EXPECT_NEAR(r.GetAsNumber(), 100.0, 1e-10);
}

TEST_F(RationalTest, NegativeNumeratorString) {
    Rational r("-3", "4");
    EXPECT_EQ(r.GetAsString(), "-3\\4");
    EXPECT_LT(r.GetAsNumber(), 0);
}

TEST_F(RationalTest, ZeroRational) {
    Rational r(0);
    Rational zero("0", "1");
    EXPECT_EQ(r, zero);
}