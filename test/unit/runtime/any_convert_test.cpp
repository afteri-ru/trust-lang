// Test file: trust::any_to (typed conversion from a dict element TypedValue by kind).
#include "trust/any_convert.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

using trust::any_to;
using trust::Rational;
using trust::TypedValue;

namespace {

// TypeKind: Group(0-7) | Data/размерность(8-15) — кодировка (ABI) trust::TypedValue.
constexpr uint32_t kInt8 = 3u | (8u << 8);
constexpr uint32_t kInt32 = 3u | (32u << 8);
constexpr uint32_t kInt64 = 3u | (64u << 8);
constexpr uint32_t kFloat64 = 5u | (64u << 8);
constexpr uint32_t kBool = 2u | (1u << 8);
constexpr uint32_t kStrChar = 9u | (1u << 8);

TEST(AnyConvertTest, NumericTarget) {
    EXPECT_EQ(any_to<int32_t>(TypedValue{kInt8, int8_t(42)}), 42);
    EXPECT_EQ(any_to<int64_t>(TypedValue{kInt64, int64_t(100)}), 100);
    EXPECT_DOUBLE_EQ(any_to<double>(TypedValue{kFloat64, 2.5}), 2.5);
    EXPECT_EQ(any_to<int>(TypedValue{kBool, true}), 1); // bool → 1
}

TEST(AnyConvertTest, StringTarget) {
    EXPECT_EQ(any_to<std::string>(TypedValue{kStrChar, std::string("hi")}), "hi");
}

TEST(AnyConvertTest, NumericFromStringThrows) {
    EXPECT_THROW(any_to<int32_t>(TypedValue{kStrChar, std::string("abc")}), std::runtime_error);
}

TEST(AnyConvertTest, StringFromNumberThrows) {
    EXPECT_THROW(any_to<std::string>(TypedValue{kInt32, int32_t(42)}), std::runtime_error);
}

TEST(AnyConvertTest, RationalTarget) {
    constexpr uint32_t kRational = 8u | (1u << 8);
    // Rational — быстрая ветка variant (по значению); any_to<Rational> читает её напрямую.
    const Rational r = any_to<Rational>(TypedValue{kRational, Rational("3", "4")});
    EXPECT_EQ(r.GetAsString(), "3\\4");
    // Числовое приведение из Rational (GetAsNumber).
    EXPECT_DOUBLE_EQ(any_to<double>(TypedValue{kRational, Rational("5", "2")}), 2.5);
}

} // namespace
