// Test file: trust::ThreadPool - schedule() awaitable on a fixed-size pool.
#include "stdlib/core/async/task.hpp"
#include "stdlib/core/async/thread_pool.hpp"

#include <atomic>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

using trust::Task;
using trust::ThreadPool;
using trust::sync_wait;

namespace {

Task<std::thread::id> resumeOnPool(ThreadPool& pool) {
    co_await pool.schedule();
    co_return std::this_thread::get_id();
}

Task<void> countOnPool(ThreadPool& pool, std::atomic<int>& counter, int iterations) {
    for (int i = 0; i < iterations; ++i) {
        co_await pool.schedule();
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

Task<bool> currentIs(ThreadPool& pool) {
    co_await pool.schedule();
    co_return ThreadPool::current() == &pool;
}

} // namespace

TEST(AsyncPoolTest, ResumesOnWorkerThread) {
    ThreadPool pool(2);
    const std::thread::id caller = std::this_thread::get_id();
    const std::thread::id worker = sync_wait(resumeOnPool(pool));
    EXPECT_NE(worker, caller);
}

TEST(AsyncPoolTest, ExecutesManyTasks) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    sync_wait(countOnPool(pool, counter, 100));
    EXPECT_EQ(counter.load(), 100);
}

TEST(AsyncPoolTest, ReportsThreadCount) {
    ThreadPool pool(3);
    EXPECT_EQ(pool.threadCount(), 3u);
}

TEST(AsyncPoolTest, RejectsZeroThreads) {
    EXPECT_THROW(ThreadPool(0), std::invalid_argument);
}

TEST(AsyncPoolTest, CurrentInsideWorker) {
    ThreadPool pool(1);
    EXPECT_TRUE(sync_wait(currentIs(pool)));
    EXPECT_EQ(ThreadPool::current(), nullptr);
}
