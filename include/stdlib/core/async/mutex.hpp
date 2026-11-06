// stdlib/async/mutex.hpp - async mutual exclusion for the Trust standard library.
// Self-contained header of a library type ("stdlib/async/mutex.hpp").
// Uses ONLY standard C++ (<mutex>, <coroutine>, <deque>).
// Design:
//   - `co_await mutex.lock()` suspends the coroutine while the mutex is held by another
//     coroutine; waiters are resumed in FIFO order.
//   - Ownership is transferred directly to the next waiter (no re-check race): `unlock()`
//     pops the first waiter and resumes it after releasing the internal lock.
//   - `try_lock()` never steals ownership from an already queued waiter.
//   - Destroying a locked mutex or a mutex with waiters is a programming error (terminate).

#pragma once

#include <coroutine>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>

namespace trust {

class AsyncMutex {
  public:
    AsyncMutex() noexcept = default;

    ~AsyncMutex();

    AsyncMutex(const AsyncMutex&) = delete;
    AsyncMutex& operator=(const AsyncMutex&) = delete;
    AsyncMutex(AsyncMutex&&) = delete;
    AsyncMutex& operator=(AsyncMutex&&) = delete;

    class LockAwaiter {
      public:
        explicit LockAwaiter(AsyncMutex* mutex) noexcept
        : m_mutex(mutex) {}

        bool await_ready() { return m_mutex->tryAcquire(); }

        bool await_suspend(std::coroutine_handle<> handle) { return m_mutex->suspend(handle); }

        void await_resume() const noexcept {}

      private:
        AsyncMutex* m_mutex;
    };

    [[nodiscard]] LockAwaiter lock() noexcept { return LockAwaiter(this); }

    [[nodiscard]] bool try_lock() noexcept { return tryAcquire(); }

    void unlock();

  private:
    bool tryAcquire();
    bool suspend(std::coroutine_handle<> handle);

    std::mutex m_mutex;
    bool m_locked = false;
    std::deque<std::coroutine_handle<>> m_waiters;
};

inline AsyncMutex::~AsyncMutex() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_locked || !m_waiters.empty()) {
        std::terminate();
    }
}

inline bool AsyncMutex::tryAcquire() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_locked && m_waiters.empty()) {
        m_locked = true;
        return true;
    }
    return false;
}

inline bool AsyncMutex::suspend(std::coroutine_handle<> handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_locked && m_waiters.empty()) {
        m_locked = true;
        return false;
    }
    m_waiters.push_back(handle);
    return true;
}

inline void AsyncMutex::unlock() {
    std::coroutine_handle<> next;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_locked) {
            throw std::logic_error("trust::AsyncMutex: unlock of an unlocked mutex");
        }
        if (m_waiters.empty()) {
            m_locked = false;
            return;
        }
        next = m_waiters.front();
        m_waiters.pop_front();
    }
    // Ownership is transferred to the next waiter; it must not re-acquire the lock.
    next.resume();
}

} // namespace trust
