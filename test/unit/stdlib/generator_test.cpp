// Test file: trust::Generator - lazy coroutine generator (R3 representation).
#include "stdlib/generator.hpp"
#include "trust/range.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

using trust::Generator;
using trust::lazyRange;

namespace {

Generator<int> makeInts() {
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

Generator<int> makeEmpty() {
    co_return;
}

Generator<int> makeThrowing() {
    co_yield 1;
    throw std::runtime_error("boom");
}

TEST(GeneratorTest, YieldsSequence) {
    int sum = 0;
    for (int value : makeInts()) {
        sum += value;
    }
    EXPECT_EQ(sum, 6);
}

TEST(GeneratorTest, EmptyGenerator) {
    int count = 0;
    for (int value : makeEmpty()) {
        (void)value;
        ++count;
    }
    EXPECT_EQ(count, 0);
}

TEST(GeneratorTest, IsSinglePassMoveOnly) {
    static_assert(!std::is_copy_constructible_v<Generator<int>>);
    static_assert(!std::is_copy_assignable_v<Generator<int>>);
    static_assert(std::is_move_constructible_v<Generator<int>>);
    static_assert(std::is_move_assignable_v<Generator<int>>);
}

TEST(GeneratorTest, MoveTransfersOwnership) {
    Generator<int> source = makeInts();
    Generator<int> moved = std::move(source);
    int sum = 0;
    for (int value : moved) {
        sum += value;
    }
    EXPECT_EQ(sum, 6);
}

TEST(GeneratorTest, PropagatesException) {
    EXPECT_THROW(
        {
            for (int value : makeThrowing()) {
                (void)value;
            }
        },
        std::runtime_error);
}

TEST(GeneratorTest, LazyRangeOverVector) {
    std::vector<int> values{4, 5, 6};
    int sum = 0;
    auto generator = lazyRange(values);
    for (int value : generator) {
        sum += value;
    }
    EXPECT_EQ(sum, 15);
}

TEST(GeneratorTest, LazyRangeOverTrustRange) {
    const trust::Range<std::int64_t> range(1, 5); // 1..5 inclusive
    int sum = 0;
    auto generator = lazyRange(range);
    for (std::int64_t value : generator) {
        sum += static_cast<int>(value);
    }
    EXPECT_EQ(sum, 15); // 1 + 2 + 3 + 4 + 5
}

} // namespace
