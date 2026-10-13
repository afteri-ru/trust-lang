// stdlib/iterator.hpp - the single universal Trust iterator implementation.
//
// Standard library header of the `Iterator<T>` library type: self-contained (standard headers
// only) so generated C++ programs can include it without the compiler's include tree.
// Delivered inside the toolchain package (include/stdlib/iterator.hpp inside share/trust/stdlib.trust)
// and available via -Istdlib/include when a generated program uses Iterator<T>.
//
// Design:
//   - `trust::Iterator<T>` is ONE type-erased cursor over ANY source sequence. It is the only
//     C++ implementation behind the Trust-level `Iterator<T>` type.
//   - Two constructors: from a range/container (`begin()/end()`) and from a pair of standard
//     C++ iterators (`first`, `last`), including `std::reverse_iterator`.
//   - C++-unified interface: `*`, prefix/postfix `++`, prefix/postfix `--`, `explicit bool`.
//     `hasNext()` is the completion check used by Trust conditions (`it.%hasNext()`).
//   - Borrowed view: the underlying sequence must outlive the iterator.
//   - Errors are explicit (`std::out_of_range` / `std::logic_error`), no silent fallbacks.

#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace trust {

/// Universal type-erased iterator producing `T` values.
template <typename T>
class Iterator {
  public:
    using value_type = T;
    using reference = T;
    using difference_type = std::ptrdiff_t;

    Iterator() noexcept = default;

    /// From a Trust object/range: uses ranges::begin/end. Rejects temporaries that cannot be
    /// borrowed (`std::vector<int>{}`) to avoid an immediately dangling cursor.
    template <std::ranges::input_range R>
        requires(std::ranges::borrowed_range<R &&> && std::convertible_to<std::ranges::range_reference_t<R>, T>)
    explicit Iterator(R&& range)
    : m_impl(std::make_unique<Model<std::ranges::iterator_t<R>, std::ranges::sentinel_t<R>>>(std::ranges::begin(range), std::ranges::end(range))) {}

    /// From standard C++ iterators [first, last).
    template <std::input_iterator It, std::sentinel_for<It> Sent>
        requires std::convertible_to<std::iter_reference_t<It>, T>
    Iterator(It first, Sent last)
    : m_impl(std::make_unique<Model<It, Sent>>(std::move(first), std::move(last))) {}

    Iterator(const Iterator& other)
    : m_impl(other.m_impl ? other.m_impl->clone() : nullptr) {}

    Iterator& operator=(const Iterator& other) {
        if (this != &other) {
            m_impl = other.m_impl ? other.m_impl->clone() : nullptr;
        }
        return *this;
    }

    Iterator(Iterator&&) noexcept = default;
    Iterator& operator=(Iterator&&) noexcept = default;
    ~Iterator() = default;

    /// Current value. Exhausted iterator -> std::out_of_range.
    [[nodiscard]] T operator*() const {
        checkActive("operator*");
        if (m_impl->atEnd()) {
            throw std::out_of_range("trust::Iterator::operator*: iterator is exhausted");
        }
        return m_impl->get();
    }

    /// One step forward. Exhausted iterator -> std::out_of_range.
    Iterator& operator++() {
        checkActive("operator++");
        if (m_impl->atEnd()) {
            throw std::out_of_range("trust::Iterator::operator++: iterator is exhausted");
        }
        m_impl->advance();
        return *this;
    }

    Iterator operator++(int) {
        Iterator copy = *this;
        ++(*this);
        return copy;
    }

    /// One step backward. Requires a bidirectional source; otherwise std::logic_error.
    Iterator& operator--() {
        checkActive("operator--");
        m_impl->retreat();
        return *this;
    }

    Iterator operator--(int) {
        Iterator copy = *this;
        --(*this);
        return copy;
    }

    /// True while the traversal is not finished (`while (it)`, `if (!++it)`).
    [[nodiscard]] explicit operator bool() const { return m_impl != nullptr && !m_impl->atEnd(); }

    /// Completion check used by Trust conditions: same as `static_cast<bool>(*this)`.
    [[nodiscard]] bool hasNext() const { return static_cast<bool>(*this); }

  private:
    struct Concept {
        Concept() = default;
        Concept(const Concept&) = delete;
        Concept& operator=(const Concept&) = delete;
        virtual ~Concept() = default;
        [[nodiscard]] virtual T get() const = 0;
        virtual void advance() = 0;
        virtual void retreat() = 0;
        [[nodiscard]] virtual bool atEnd() const = 0;
        [[nodiscard]] virtual std::unique_ptr<Concept> clone() const = 0;
    };

    template <std::input_iterator It, std::sentinel_for<It> Sent>
    struct Model final : Concept {
        It m_first;
        Sent m_last;

        Model(It first, Sent last)
        : m_first(std::move(first))
        , m_last(std::move(last)) {}

        [[nodiscard]] T get() const override { return *m_first; }
        void advance() override { ++m_first; }
        void retreat() override {
            if constexpr (std::bidirectional_iterator<It>) {
                --m_first;
            } else {
                throw std::logic_error("trust::Iterator::operator--: source is not bidirectional");
            }
        }
        [[nodiscard]] bool atEnd() const override { return m_first == m_last; }
        [[nodiscard]] std::unique_ptr<Concept> clone() const override {
            if constexpr (std::copy_constructible<It> && std::copy_constructible<Sent>) {
                return std::make_unique<Model>(m_first, m_last);
            } else {
                throw std::logic_error("trust::Iterator: source iterators are not copyable");
            }
        }
    };

    void checkActive(const char* operation) const {
        if (m_impl != nullptr) {
            return;
        }
        if (std::string_view(operation) == "operator++") {
            throw std::out_of_range("trust::Iterator::operator++: iterator is empty");
        }
        if (std::string_view(operation) == "operator--") {
            throw std::out_of_range("trust::Iterator::operator--: iterator is empty");
        }
        throw std::out_of_range("trust::Iterator::operator*: iterator is empty");
    }

    std::unique_ptr<Concept> m_impl;
};

/// Factory: iterator from standard C++ iterators.
template <typename T, std::input_iterator It, std::sentinel_for<It> Sent>
[[nodiscard]] Iterator<T> makeIterator(It first, Sent last) {
    return Iterator<T>(std::move(first), std::move(last));
}

/// Factory: iterator from a range/container (borrowed view).
template <typename T, std::ranges::borrowed_range R>
[[nodiscard]] Iterator<T> makeIterator(R&& range) {
    return Iterator<T>(std::forward<R>(range));
}

} // namespace trust
