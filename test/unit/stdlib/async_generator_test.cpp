// Test file: trust::AsyncGenerator - pull-based generator that may co_await.
#include "stdlib/core/async/generator.hpp"
#include "stdlib/core/async/task.hpp"

#include <optional>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

using trust::AsyncGenerator;
using trust::Generator;
using trust::Task;
using trust::sync_wait;

namespace {

Task<int> valueTask(int value) {
    co_return value;
}

AsyncGenerator<int> sequenceWithAwait() {
    int first = co_await valueTask(10);
    co_yield first;
    int second = co_await valueTask(20);
    co_yield second;
}

AsyncGenerator<int> throwingAfterOne() {
    co_yield 1;
    co_await valueTask(0);
    throw std::runtime_error("boom");
}

AsyncGenerator<int> emptySequence() {
    co_return;
}

} // namespace

TEST(AsyncGeneratorTest, AwaitBetweenYields) {
    AsyncGenerator<int> generator = sequenceWithAwait();
    std::vector<int> values;
    while (true) {
        std::optional<int> next = sync_wait(generator.next());
        if (!next.has_value()) {
            break;
        }
        values.push_back(*next);
    }
    EXPECT_EQ(values, (std::vector<int>{10, 20}));
}

TEST(AsyncGeneratorTest, EmptySequence) {
    AsyncGenerator<int> generator = emptySequence();
    EXPECT_FALSE(sync_wait(generator.next()).has_value());
}

TEST(AsyncGeneratorTest, PropagatesException) {
    AsyncGenerator<int> generator = throwingAfterOne();
    EXPECT_EQ(sync_wait(generator.next()).value(), 1);
    EXPECT_THROW(sync_wait(generator.next()), std::runtime_error);
}

TEST(AsyncGeneratorTest, RepeatedNextAfterErrorRethrows) {
    AsyncGenerator<int> generator = throwingAfterOne();
    EXPECT_EQ(sync_wait(generator.next()).value(), 1);
    EXPECT_THROW(sync_wait(generator.next()), std::runtime_error);
    EXPECT_THROW(sync_wait(generator.next()), std::runtime_error);
}

TEST(AsyncGeneratorTest, MoveOnly) {
    static_assert(!std::is_copy_constructible_v<AsyncGenerator<int>>);
    static_assert(std::is_move_constructible_v<AsyncGenerator<int>>);
    static_assert(!std::is_same_v<AsyncGenerator<int>, Generator<int>>);
}

TEST(AsyncGeneratorTest, EarlyDestroyKeepsFrameSafe) {
    {
        AsyncGenerator<int> generator = sequenceWithAwait();
        EXPECT_EQ(sync_wait(generator.next()).value(), 10);
    }
    SUCCEED();
}
