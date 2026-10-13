// Test file: trust::AsyncMutex - FIFO async mutual exclusion.
#include "stdlib/core/async/mutex.hpp"
#include "stdlib/core/async/task.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

using trust::AsyncMutex;
using trust::Task;
using trust::sync_wait;

namespace {

Task<void> lockAndHold(AsyncMutex& mutex, int& counter, std::atomic<int>& entered) {
    co_await mutex.lock();
    entered.fetch_add(1, std::memory_order_relaxed);
    const int value = counter;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    counter = value + 1;
    mutex.unlock();
}

} // namespace

TEST(AsyncMutexTest, MutualExclusionAcrossThreads) {
    AsyncMutex mutex;
    int counter = 0;
    std::atomic<int> entered{0};

    std::thread first([&] { sync_wait(lockAndHold(mutex, counter, entered)); });
    std::thread second([&] { sync_wait(lockAndHold(mutex, counter, entered)); });
    first.join();
    second.join();

    EXPECT_EQ(counter, 2);
    EXPECT_EQ(entered.load(), 2);
}

TEST(AsyncMutexTest, UnlockWithoutLockFaults) {
    AsyncMutex mutex;
    EXPECT_THROW(mutex.unlock(), std::logic_error);
}

TEST(AsyncMutexTest, TryLockThenUnlock) {
    AsyncMutex mutex;
    EXPECT_TRUE(mutex.try_lock());
    EXPECT_FALSE(mutex.try_lock());
    mutex.unlock();
    EXPECT_TRUE(mutex.try_lock());
    mutex.unlock();
}
