// Test file: trust::Task / trust::sync_wait - lazy coroutine task.
#include "stdlib/core/async/task.hpp"

#include <coroutine>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

using trust::Task;
using trust::sync_wait;

namespace {

Task<int> readyValue(int value) {
    co_return value;
}

Task<void> readyVoid(bool& flag) {
    flag = true;
    co_return;
}

Task<int> throwing() {
    co_await std::suspend_never{};
    if (true) {
        throw std::runtime_error("boom");
    }
    co_return 0;
}

Task<int> nested(int value) {
    const int doubled = co_await readyValue(value * 2);
    co_return doubled + 1;
}

Task<std::string> moveOnlyResult() {
    co_return std::string("payload");
}

} // namespace

TEST(AsyncTaskTest, ReturnsValue) {
    EXPECT_EQ(sync_wait(readyValue(42)), 42);
}

TEST(AsyncTaskTest, ReturnsVoid) {
    bool flag = false;
    sync_wait(readyVoid(flag));
    EXPECT_TRUE(flag);
}

TEST(AsyncTaskTest, PropagatesException) {
    EXPECT_THROW(sync_wait(throwing()), std::runtime_error);
}

TEST(AsyncTaskTest, NestedAwait) {
    EXPECT_EQ(sync_wait(nested(20)), 41);
}

TEST(AsyncTaskTest, MoveOnlyResult) {
    EXPECT_EQ(sync_wait(moveOnlyResult()), std::string("payload"));
}

TEST(AsyncTaskTest, EmptyTaskFaults) {
    Task<int> empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_THROW(sync_wait(std::move(empty)), std::logic_error);
}

TEST(AsyncTaskTest, IsMoveOnly) {
    static_assert(!std::is_copy_constructible_v<Task<int>>);
    static_assert(!std::is_copy_assignable_v<Task<int>>);
    static_assert(std::is_move_constructible_v<Task<int>>);
    static_assert(std::is_move_assignable_v<Task<int>>);
}

TEST(AsyncTaskTest, LazyUntilStarted) {
    bool ran = false;
    {
        Task<void> task = readyVoid(ran);
        (void)task;
    }
    EXPECT_FALSE(ran);
}
