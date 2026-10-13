// stdlib/async/thread_pool.hpp - fixed-size thread pool for the Trust standard library.
//
// Self-contained header of a library type ("stdlib/async/thread_pool.hpp").
// Uses ONLY standard C++ (<thread>, <mutex>, <condition_variable>, <coroutine>).
//
// Precondition: the pool must outlive every coroutine scheduled on it. Destruction drains the
// queue and joins the workers; coroutines that try to schedule after shutdown get an explicit
// exception at the `co_await` point.
//
// Design:
//   - `ThreadPool(n)` starts `n` worker threads with a FIFO task queue.
//   - `co_await pool.schedule()` suspends the current coroutine and resumes it on a pool
//     worker (symmetric hand-off through a coroutine_handle).
//   - Destruction stops the pool: queued coroutines are drained (resumed) and workers are
//     joined. Scheduling after shutdown is an explicit error, never a silent drop.

#pragma once

#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace trust {

class ThreadPool {
  public:
    explicit ThreadPool(std::size_t thread_count = defaultThreadCount());

    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    [[nodiscard]] std::size_t threadCount() const noexcept { return m_workers.size(); }

    class ScheduleAwaiter {
      public:
        explicit ScheduleAwaiter(ThreadPool* pool) noexcept
        : m_pool(pool) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) { m_pool->enqueue(handle); }

        void await_resume() const noexcept {}

      private:
        ThreadPool* m_pool;
    };

    [[nodiscard]] ScheduleAwaiter schedule() noexcept { return ScheduleAwaiter(this); }

    /// Pool of the currently executing worker thread (nullptr outside the pool).
    [[nodiscard]] static ThreadPool* current() noexcept { return t_current; }

  private:
    static std::size_t defaultThreadCount() noexcept;

    void enqueue(std::coroutine_handle<> handle);
    void workerLoop();

    static inline thread_local ThreadPool* t_current = nullptr;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_queue;
    std::vector<std::thread> m_workers;
    bool m_stopping = false;
};

inline std::size_t ThreadPool::defaultThreadCount() noexcept {
    const unsigned int count = std::thread::hardware_concurrency();
    return count == 0 ? 1 : static_cast<std::size_t>(count);
}

inline ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
        throw std::invalid_argument("trust::ThreadPool: thread count must be greater than zero");
    }
    try {
        m_workers.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            m_workers.emplace_back([this] { workerLoop(); });
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_cv.notify_all();
        for (std::thread& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        throw;
    }
}

inline ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_cv.notify_all();
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

inline void ThreadPool::enqueue(std::coroutine_handle<> handle) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) {
            throw std::runtime_error("trust::ThreadPool: cannot schedule work after shutdown");
        }
        m_queue.emplace_back([handle] { handle.resume(); });
    }
    m_cv.notify_one();
}

inline void ThreadPool::workerLoop() {
    t_current = this;
    for (;;) {
        std::function<void()> work;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_queue.empty()) {
                if (m_stopping) {
                    return;
                }
                continue;
            }
            work = std::move(m_queue.front());
            m_queue.pop_front();
        }
        work();
    }
}

} // namespace trust
