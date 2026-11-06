// stdlib/async/generator.hpp - lazy coroutine generator for the Trust runtime.
// Standard library header of a library type ("stdlib/async/generator.hpp"): self-contained.
// Uses ONLY <coroutine> (NOT <generator>) because the
// default compiler of generated programs (c++ = GCC 13) does not ship std::generator, while
// the project's build compiler (clang++-22 over libstdc++ 14) does. Keeping to <coroutine>
// makes the header usable by both.
// Design:
//   - `Generator<T>` is a single-pass lazy sequence produced by `co_yield` (coroutines) for
//     lazy iteration over long/streaming sequences (e.g. Range).
//   - Move-only (single-pass): copying would alias the coroutine frame.
//   - `lazyRange(range)` produces a lazy Generator over any input range.
//   - Exceptions thrown inside the coroutine are re-thrown to the consumer on iteration.

#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "task.hpp"

namespace trust {

/// Lazy single-pass sequence of `T` produced by a coroutine (`co_yield`).
template <typename T>
class Generator {
  public:
    struct promise_type {
        T* m_value = nullptr;
        std::exception_ptr m_error;

        Generator get_return_object() noexcept { return Generator(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T& value) noexcept {
            m_value = std::addressof(value);
            return {};
        }
        std::suspend_always yield_value(T&& value) noexcept {
            m_value = std::addressof(value);
            return {};
        }
        void return_void() noexcept {}
        void unhandled_exception() { m_error = std::current_exception(); }
    };

    using HandleType = std::coroutine_handle<promise_type>;

    Generator() noexcept = default;

    explicit Generator(HandleType handle) noexcept
    : m_handle(handle) {}

    Generator(Generator&& other) noexcept
    : m_handle(std::exchange(other.m_handle, {}))
    , m_started(std::exchange(other.m_started, false)) {}

    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle = std::exchange(other.m_handle, {});
            m_started = std::exchange(other.m_started, false);
        }
        return *this;
    }

    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    ~Generator() {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    /// Input iterator over the coroutine-produced sequence.
    class iterator {
      public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;

        iterator() noexcept = default;

        explicit iterator(HandleType handle) noexcept
        : m_handle(handle) {}

        [[nodiscard]] const T& operator*() const {
            if (!m_handle || m_handle.done() || m_handle.promise().m_value == nullptr) {
                throw std::logic_error("trust::Generator: no value to dereference");
            }
            return *m_handle.promise().m_value;
        }

        iterator& operator++() {
            if (!m_handle || m_handle.done()) {
                throw std::logic_error("trust::Generator: increment past the end");
            }
            m_handle.resume();
            rethrowIfFailed();
            return *this;
        }

        void operator++(int) { ++(*this); }

        friend bool operator==(const iterator& it, std::default_sentinel_t) { return !it.m_handle || it.m_handle.done(); }
        friend bool operator==(std::default_sentinel_t sentinel, const iterator& it) { return it == sentinel; }

      private:
        HandleType m_handle{};

        void rethrowIfFailed() const {
            if (m_handle.done() && m_handle.promise().m_error) {
                std::rethrow_exception(m_handle.promise().m_error);
            }
        }
    };

    [[nodiscard]] iterator begin() {
        if (m_started) {
            throw std::logic_error("trust::Generator: begin() may be called only once");
        }
        m_started = true;
        if (!m_handle) {
            return iterator{};
        }
        m_handle.resume();
        if (m_handle.done() && m_handle.promise().m_error) {
            std::rethrow_exception(m_handle.promise().m_error);
        }
        return iterator{m_handle};
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept { return {}; }

  private:
    HandleType m_handle{};
    bool m_started = false;
};

/// Async generator: pull-based (`next()`) coroutine that may `co_await` between
/// `co_yield`s. The consumer is a `next()` coroutine; producer/consumer hand-off uses
/// symmetric transfer, so an await that suspends on another thread is fully supported.
/// Lifetime: a started `next()` task must be awaited to completion; destroying it while it is
/// in flight leaves the generator with a dangling consumer (a subsequent `next()` throws).
template <typename T>
class AsyncGenerator {
  public:
    struct promise_type {
        std::coroutine_handle<> m_consumer{};
        T* m_value = nullptr;
        std::exception_ptr m_error;
        bool m_finished = false;

        AsyncGenerator get_return_object() noexcept { return AsyncGenerator(std::coroutine_handle<promise_type>::from_promise(*this)); }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                promise_type& promise = handle.promise();
                promise.m_finished = true;
                std::coroutine_handle<> consumer = promise.m_consumer;
                promise.m_consumer = {};
                return consumer ? consumer : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }

        struct YieldAwaiter {
            promise_type* m_promise;

            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type>) noexcept {
                std::coroutine_handle<> consumer = m_promise->m_consumer;
                m_promise->m_consumer = {};
                return consumer ? consumer : std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        YieldAwaiter yield_value(T& value) noexcept {
            m_value = std::addressof(value);
            return YieldAwaiter{this};
        }

        YieldAwaiter yield_value(T&& value) noexcept {
            m_value = std::addressof(value);
            return YieldAwaiter{this};
        }

        void return_void() noexcept {}

        void unhandled_exception() { m_error = std::current_exception(); }
    };

    using HandleType = std::coroutine_handle<promise_type>;

    AsyncGenerator() noexcept = default;

    explicit AsyncGenerator(HandleType handle) noexcept
    : m_handle(handle) {}

    AsyncGenerator(AsyncGenerator&& other) noexcept
    : m_handle(std::exchange(other.m_handle, {})) {}

    AsyncGenerator& operator=(AsyncGenerator&& other) noexcept {
        if (this != &other) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle = std::exchange(other.m_handle, {});
        }
        return *this;
    }

    AsyncGenerator(const AsyncGenerator&) = delete;
    AsyncGenerator& operator=(const AsyncGenerator&) = delete;

    ~AsyncGenerator() {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    /// Produces the next value, or `std::nullopt` when the generator is finished.
    /// The returned task must be awaited: it is the consumer side of the producer/consumer
    /// hand-off, and a second concurrent `next()` is an explicit error.
    [[nodiscard]] Task<std::optional<T>> next() {
        if (!m_handle) {
            co_return std::nullopt;
        }
        if (m_handle.done()) {
            if (m_handle.promise().m_error) {
                std::rethrow_exception(m_handle.promise().m_error);
            }
            co_return std::nullopt;
        }
        co_await ResumeProducer{m_handle};

        auto& promise = m_handle.promise();
        if (promise.m_error) {
            std::rethrow_exception(promise.m_error);
        }
        if (promise.m_finished) {
            co_return std::nullopt;
        }
        if (promise.m_value == nullptr) {
            throw std::logic_error("trust::AsyncGenerator: producer resumed without a value");
        }
        co_return std::optional<T>(*promise.m_value);
    }

  private:
    struct ResumeProducer {
        HandleType m_handle;

        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> consumer) {
            promise_type& promise = m_handle.promise();
            if (promise.m_consumer) {
                throw std::logic_error("trust::AsyncGenerator: concurrent next() is not supported");
            }
            promise.m_consumer = consumer;
            return m_handle;
        }

        void await_resume() const noexcept {}
    };

    HandleType m_handle{};
};

/// Ленивый Generator над любым входным диапазоном (элементы копируются в значение T).
template <std::ranges::input_range R>
Generator<std::ranges::range_value_t<R>> lazyRange(R& range) {
    for (auto&& item : range) {
        co_yield std::ranges::range_value_t<R>(item);
    }
}

} // namespace trust
