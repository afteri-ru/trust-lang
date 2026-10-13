// stdlib/async/task.hpp - lazy coroutine task for the Trust standard library.
//
// Self-contained header of a library type ("stdlib/async/task.hpp").
// Uses ONLY standard C++20 (<coroutine>, <condition_variable>, <mutex>): the default
// compiler of generated programs (c++ = GCC 13) has no std::execution and no
// std::generator, so the async layer is implemented on the coroutine machinery directly.
//
// Design:
//   - `Task<T>` is a lazy, move-only coroutine handle: it starts when awaited or when
//     passed to `sync_wait`.
//   - Awaiting a task consumes it (`operator co_await() &&`); the awaiter owns the frame
//     and destroys it after the result is consumed.
//   - Completion is delivered by the `sync_wait` driver coroutine, which owns the awaited
//     frame and is destroyed by the coroutine machinery itself - never by the waiting thread.
//   - Exceptions are stored in the promise and rethrown at the await point / in sync_wait.

#pragma once

#include <condition_variable>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace trust {

template <typename T>
class Task;

namespace detail {

/// Storage for `sync_wait`: `std::optional<T>`; no storage for `void`.
template <typename T>
struct SyncWaitStorage {
    std::optional<T> value;
};

template <>
struct SyncWaitStorage<void> {};

/// Completion state shared by `sync_wait` and its driver coroutine.
/// The driver writes the result, then signals; the mutex release/acquire pair makes the
/// result visible to the waiting thread.
template <typename T>
struct SyncWaitState : SyncWaitStorage<T> {
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done = false;
    std::exception_ptr m_error;

    void notify() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_all();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_done; });
    }
};

/// Common promise state: continuation and exception storage.
class TaskPromiseBase {
  public:
    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            std::coroutine_handle<> continuation = handle.promise().continuation();
            return continuation ? continuation : std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    void unhandled_exception() noexcept { m_exception = std::current_exception(); }

    void setContinuation(std::coroutine_handle<> continuation) noexcept { m_continuation = continuation; }

    [[nodiscard]] std::coroutine_handle<> continuation() const noexcept { return m_continuation; }

    [[nodiscard]] const std::exception_ptr& exception() const noexcept { return m_exception; }

  private:
    std::coroutine_handle<> m_continuation{};
    std::exception_ptr m_exception;
};

template <typename T>
struct TaskPromise final : TaskPromiseBase {
    static_assert(!std::is_reference_v<T>, "trust::Task<T> does not support reference result types");
    static_assert(!std::is_void_v<T>, "use the Task<void> specialization for void results");

    std::optional<T> m_value;

    Task<T> get_return_object() noexcept;

    void return_value(T value) { m_value.emplace(std::move(value)); }
};

template <>
struct TaskPromise<void> final : TaskPromiseBase {
    Task<void> get_return_object() noexcept;

    void return_void() noexcept {}
};

} // namespace detail

/// Lazy move-only coroutine returning exactly one value (or throwing).
template <typename T>
class Task {
  public:
    using promise_type = detail::TaskPromise<T>;
    using Handle = std::coroutine_handle<promise_type>;

    Task() noexcept = default;

    explicit Task(Handle handle) noexcept
    : m_handle(handle) {}

    Task(Task&& other) noexcept
    : m_handle(std::exchange(other.m_handle, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle = std::exchange(other.m_handle, {});
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    [[nodiscard]] bool empty() const noexcept { return !m_handle; }

    /// Awaiter: owns the coroutine frame and destroys it after resumption.
    class Awaiter {
      public:
        explicit Awaiter(Handle handle) noexcept
        : m_handle(handle) {}

        Awaiter(Awaiter&& other) noexcept
        : m_handle(std::exchange(other.m_handle, {})) {}

        Awaiter(const Awaiter&) = delete;
        Awaiter& operator=(const Awaiter&) = delete;
        Awaiter& operator=(Awaiter&&) = delete;

        ~Awaiter() {
            if (m_handle) {
                m_handle.destroy();
            }
        }

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) {
            if (!m_handle) {
                throw std::logic_error("trust::Task: awaiting an empty task");
            }
            m_handle.promise().setContinuation(continuation);
            return m_handle;
        }

        decltype(auto) await_resume() {
            if (!m_handle) {
                throw std::logic_error("trust::Task: awaiting an empty task");
            }
            auto& promise = m_handle.promise();
            if (promise.exception()) {
                std::rethrow_exception(promise.exception());
            }
            if constexpr (std::is_void_v<T>) {
                return;
            } else {
                if (!promise.m_value.has_value()) {
                    throw std::logic_error("trust::Task: coroutine finished without a value");
                }
                return T(std::move(*promise.m_value));
            }
        }

      private:
        Handle m_handle{};
    };

    /// Tasks are awaitable as rvalues only: awaiting consumes the coroutine frame.
    [[nodiscard]] Awaiter operator co_await() && {
        if (!m_handle) {
            throw std::logic_error("trust::Task: awaiting an empty task");
        }
        return Awaiter(std::exchange(m_handle, {}));
    }

  private:
    Handle m_handle{};
};

template <typename T>
Task<T> detail::TaskPromise<T>::get_return_object() noexcept {
    return Task<T>(Task<T>::Handle::from_promise(*this));
}

inline Task<void> detail::TaskPromise<void>::get_return_object() noexcept {
    return Task<void>(Task<void>::Handle::from_promise(*this));
}

namespace detail {

/// Fire-and-forget driver coroutine: owns the task, writes the result into `state`, then
/// signals. `final_suspend` is `suspend_never`, so the driver (and the awaited task frame
/// it owns) is destroyed by the coroutine machinery itself - never by the waiting thread.
struct SyncWaitCoroutine {
    struct promise_type {
        SyncWaitCoroutine get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
};

template <typename T>
SyncWaitCoroutine syncWaitDriver(Task<T> task, std::shared_ptr<SyncWaitState<T>> state) {
    try {
        if constexpr (std::is_void_v<T>) {
            co_await std::move(task);
        } else {
            state->value.emplace(co_await std::move(task));
        }
    } catch (...) {
        state->m_error = std::current_exception();
    }
    state->notify();
}

} // namespace detail

/// Blocks the current thread until the task completes and returns its result.
/// Must not be called from a ThreadPool worker (it blocks the worker).
template <typename T>
T sync_wait(Task<T> task) {
    if (task.empty()) {
        throw std::logic_error("trust::sync_wait: task is empty");
    }

    auto state = std::make_shared<detail::SyncWaitState<T>>();
    detail::syncWaitDriver<T>(std::move(task), state);
    state->wait();

    if (state->m_error) {
        std::rethrow_exception(state->m_error);
    }
    if constexpr (std::is_void_v<T>) {
        return;
    } else {
        if (!state->value.has_value()) {
            throw std::logic_error("trust::sync_wait: coroutine finished without a value");
        }
        return std::move(*state->value);
    }
}

} // namespace trust

