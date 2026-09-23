// stdlib/generator.hpp - lazy coroutine generator for the Trust runtime.
//
// Standard library header of a library type (stdlib asset, "stdlib/generator.hpp"): self-contained.
// Uses ONLY <coroutine> (NOT <generator>) because the
// default compiler of generated programs (c++ = GCC 13) does not ship std::generator, while
// the project's build compiler (clang++-22 over libstdc++ 14) does. Keeping to <coroutine>
// makes the header usable by both.
//
// Design:
//   - `Generator<T>` is a single-pass lazy sequence produced by `co_yield` (coroutines) - the
//     R3 representation for lazy iteration over long/streaming sequences (e.g. Range).
//   - Move-only (single-pass): copying would alias the coroutine frame.
//   - `lazyRange(range)` produces a lazy Generator over any input range.
//   - Exceptions thrown inside the coroutine are re-thrown to the consumer on iteration.

#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

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
    : m_handle(std::exchange(other.m_handle, {})) {}

    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle = std::exchange(other.m_handle, {});
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

        [[nodiscard]] const T& operator*() const { return *m_handle.promise().m_value; }

        iterator& operator++() {
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
};

/// Ленивый Generator над любым входным диапазоном (элементы копируются в значение T).
template <std::ranges::input_range R>
Generator<std::ranges::range_value_t<R>> lazyRange(R& range) {
    for (auto&& item : range) {
        co_yield std::ranges::range_value_t<R>(item);
    }
}

} // namespace trust
