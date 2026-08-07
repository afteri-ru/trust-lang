// Test file: Rational runtime type.
#include "trust/rational.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <stdexcept>

using trust::Rational;

namespace {

// GetAsString: "<num>\<den>" (backslash separator), already reduced.
TEST(RationalTest, DefaultCtor) {
    Rational r;
    EXPECT_EQ(r.GetAsString(), "0\\1");
    EXPECT_FALSE(r.GetAsBoolean()); // 0 -> false
    EXPECT_TRUE(r.isInteger());
}

TEST(RationalTest, IntCtor) {
    Rational r(42);
    EXPECT_EQ(r.GetAsString(), "42\\1");
    EXPECT_TRUE(r.GetAsBoolean());
    EXPECT_EQ(r.GetAsInteger(), 42);
}

TEST(RationalTest, StringCtorAndReduce) {
    EXPECT_EQ(Rational("2", "4").GetAsString(), "1\\2");
    EXPECT_EQ(Rational("3", "4").GetAsString(), "3\\4");
    EXPECT_EQ(Rational("10", "2").GetAsString(), "5\\1");
    // Negative denominator is normalized into the numerator.
    EXPECT_EQ(Rational("1", "-2").GetAsString(), "-1\\2");
    // Zero becomes 0/1.
    EXPECT_EQ(Rational("0", "5").GetAsString(), "0\\1");
}

TEST(RationalTest, StringValueCtor) {
    // Однострочная форма рационального литерала "num\den" парсится внутри.
    EXPECT_EQ(Rational("1\\1").GetAsString(), "1\\1");
    EXPECT_EQ(Rational("3\\4").GetAsString(), "3\\4");
    EXPECT_EQ(Rational("10\\2").GetAsString(), "5\\1");
    EXPECT_EQ(Rational("-55\\3").GetAsString(), "-55\\3");
    // Без разделителя — ошибка.
    EXPECT_THROW(Rational("no-slash"), std::runtime_error);
}

TEST(RationalTest, OperatorStringConversion) {
    // Неявное приведение к std::string (символьное представление "num\den").
    EXPECT_EQ(std::string(Rational("3", "4")), "3\\4");
    std::string s = Rational("1", "2");
    EXPECT_EQ(s, "1\\2");
    EXPECT_EQ(static_cast<std::string>(Rational("10", "2")), "5\\1");
}

TEST(RationalTest, Addition) {
    Rational s = Rational("3", "4") + Rational("1", "4");
    EXPECT_EQ(s.GetAsString(), "1\\1");
    EXPECT_EQ((Rational("1", "2") + Rational("1", "3")).GetAsString(), "5\\6");
}

TEST(RationalTest, Subtraction) {
    EXPECT_EQ((Rational("1", "2") - Rational("1", "3")).GetAsString(), "1\\6");
}

TEST(RationalTest, Multiplication) {
    EXPECT_EQ((Rational("1", "2") * Rational("2", "3")).GetAsString(), "1\\3");
}

TEST(RationalTest, Division) {
    EXPECT_EQ((Rational("1", "2") / Rational("1", "4")).GetAsString(), "2\\1");
}

TEST(RationalTest, Comparison) {
    EXPECT_TRUE(Rational("1", "2") < Rational("2", "3"));
    EXPECT_TRUE(Rational("2", "3") > Rational("1", "2"));
    EXPECT_TRUE(Rational("1", "2") == Rational("2", "4"));
    EXPECT_TRUE(Rational("1", "2") != Rational("1", "3"));
    EXPECT_TRUE(Rational("1", "2") <= Rational("1", "2"));
    EXPECT_TRUE(Rational("1", "2") >= Rational("1", "3"));
}

TEST(RationalTest, Reciprocal) {
    EXPECT_EQ(Rational("2", "3").reciprocal().GetAsString(), "3\\2");
    EXPECT_EQ(Rational("-2", "3").reciprocal().GetAsString(), "-3\\2");
}

TEST(RationalTest, Abs) {
    EXPECT_EQ(Rational::abs(Rational("-1", "2")).GetAsString(), "1\\2");
    EXPECT_EQ(Rational::abs(Rational("1", "2")).GetAsString(), "1\\2");
}

TEST(RationalTest, Conversions) {
    EXPECT_EQ(Rational("10", "2").GetAsInteger(), 5);
    EXPECT_DOUBLE_EQ(Rational("1", "4").GetAsNumber(), 0.25);
    EXPECT_TRUE(Rational(7).GetAsBoolean());
    EXPECT_FALSE(Rational(0).GetAsBoolean());
    EXPECT_FALSE(Rational("1", "2").isInteger());
    EXPECT_TRUE(Rational(3).isInteger());
}

TEST(RationalTest, CopyAndAssign) {
    Rational a("3", "4");
    Rational b(a); // copy ctor
    EXPECT_EQ(b.GetAsString(), "3\\4");
    Rational c;
    c = a; // copy assignment
    EXPECT_EQ(c.GetAsString(), "3\\4");
    c = Rational("1", "7"); // move assignment
    EXPECT_EQ(c.GetAsString(), "1\\7");
}

TEST(RationalTest, ThrowsOnZeroDenominator) {
    EXPECT_THROW(Rational("1", "0"), std::runtime_error);
}

TEST(RationalTest, ThrowsOnDivisionByZero) {
    EXPECT_THROW(Rational(1) / Rational(0), std::runtime_error);
}

} // namespace
