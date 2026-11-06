// test/unit/diag/flag_values_test.cpp
// R9: недопустимое значение поведенческой опции — ошибка (nullopt/false), а не тихий
// fallback. Единый разбор значений флагов (`-f<name>=<value>`), используемый драйвером
// и валидаторами реестра.
#include "diag/flag_values.hpp"

#include <gtest/gtest.h>

#include <cstddef>

namespace trust {
namespace {

TEST(FlagValues, BoolAcceptsOnlyEmptyOnOff) {
    ASSERT_TRUE(parseBoolFlagValue("").has_value());
    EXPECT_TRUE(*parseBoolFlagValue(""));
    ASSERT_TRUE(parseBoolFlagValue("on").has_value());
    EXPECT_TRUE(*parseBoolFlagValue("on"));
    ASSERT_TRUE(parseBoolFlagValue("off").has_value());
    EXPECT_FALSE(*parseBoolFlagValue("off"));
    // Иное значение — ошибка, а не тихое приведение к истине/лжи.
    EXPECT_FALSE(parseBoolFlagValue("yes").has_value());
    EXPECT_FALSE(parseBoolFlagValue("1").has_value());
    EXPECT_FALSE(parseBoolFlagValue("true").has_value());
    EXPECT_FALSE(parseBoolFlagValue("OFF").has_value());
}

TEST(FlagValues, SizeRejectsInvalidWithoutFallback) {
    std::size_t out = 0;
    EXPECT_TRUE(parseSizeValue("0", out));
    EXPECT_EQ(out, 0u);
    EXPECT_TRUE(parseSizeValue("20", out));
    EXPECT_EQ(out, 20u);

    EXPECT_FALSE(parseSizeValue("", out));
    EXPECT_FALSE(parseSizeValue("-1", out));
    EXPECT_FALSE(parseSizeValue("1x", out));
    EXPECT_FALSE(parseSizeValue(" 5", out));
    EXPECT_FALSE(parseSizeValue("0x10", out));

    // При ошибке выходное значение не изменяется (нет тихого приведения).
    out = 7;
    EXPECT_FALSE(parseSizeValue("abc", out));
    EXPECT_EQ(out, 7u);
}

} // namespace
} // namespace trust
