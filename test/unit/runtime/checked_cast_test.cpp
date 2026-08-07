// Test file: trust::checked_cast (range-checked numeric cast).
// Регрессия: сужение unsigned → signed не должно ложно срабатывать на нижней границе.
#include "trust/checked_cast.hpp"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>

using trust::checked_cast;

namespace {

TEST(CheckedCastTest, InRangeSignedNarrowing) {
    EXPECT_EQ(checked_cast<int8_t>(int(42)), 42);
    EXPECT_EQ(checked_cast<int16_t>(int64_t(1000)), 1000);
    EXPECT_EQ(checked_cast<int32_t>(int64_t(5)), 5);
}

// unsigned → signed сужение: min<To> отрицательный, для беззнакового From нижняя
// граница не должна применяться (иначе static_cast<From>(min<To>) оборачивается в
// огромное число и `v < huge` ложно срабатывает → abort).
TEST(CheckedCastTest, UnsignedToSignedNarrowingNoFalseAbort) {
    EXPECT_EQ(checked_cast<int32_t>(std::size_t(3)), 3);
    EXPECT_EQ(checked_cast<int16_t>(std::size_t(1000)), 1000);
    EXPECT_EQ(checked_cast<int8_t>(std::size_t(100)), 100);
}

} // namespace
