// stdlib/iterator.hpp - universal iterator-cursor (bundled range) for the Trust runtime.
//
// Standard library header of a library type: self-contained (standard headers only) so that
// generated C++ programs can include it without depending on the compiler's include tree.
// Embedded into the COMPILER as a stdlib asset (src/assets/asset_provider.cpp, table
// TRUST_ASSET_LIST, published as "@stdlib/iterator.hpp") and extracted into a temporary
// `stdlib/` directory when a generated program actually uses the Iterator type.
//
// Design:
//   - `IteratorRange<It, Sent>` is a SINGLE object that owns both the current position and
//     the end of the sequence, so the completion check does not need the source container
//     (`while (it) { use(*it); ++it; }`).
//   - Trivial conversion from native C++ iterators: `IteratorRange(first, last)` and from any
//     range/container (a borrowed view - it does NOT own the data).
//   - Access scenarios: read-only/mutable (`reference` deduced from `It`), forward /
//     bidirectional / random-access (conditional members via `requires`), range-for
//     (`begin()`/`end()` iterate the remaining sequence).
//   - Errors are reported with standard exceptions (std::out_of_range), like trust::Range.

#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace trust {

/// Universal iterator-cursor over `[first, last)`: a single value that keeps the current
/// position together with the end sentinel. It is a borrowed view - the underlying sequence
/// must outlive the cursor (matching the TrustLang `@[borrowed]` contract).
template <std::input_iterator It, std::sentinel_for<It> Sent = It>
class IteratorRange {
  public:
    using iterator = It;
    using sentinel = Sent;
    using value_type = std::iter_value_t<It>;
    using reference = std::iter_reference_t<It>;
    using difference_type = std::iter_difference_t<It>;

    IteratorRange() = default;

    /// Из нативных C++-итераторов [first, last).
    IteratorRange(It first, Sent last)
    : m_first(std::move(first))
    , m_last(std::move(last)) {}

    /// Из диапазона/контейнера (borrowed view: НЕ владеет данными).
    template <std::ranges::range R>
        requires(!std::same_as<std::remove_cvref_t<R>, IteratorRange> && std::convertible_to<std::ranges::iterator_t<R>, It>)
    explicit IteratorRange(R&& range)
    : m_first(std::ranges::begin(range))
    , m_last(std::ranges::end(range)) {}

    /// Завершён ли обход (позиция совпала с концом).
    [[nodiscard]] bool empty() const { return m_first == m_last; }

    /// Истина, пока обход не завершён (для `while (it) { ... ++it; }`).
    [[nodiscard]] explicit operator bool() const { return !empty(); }

    [[nodiscard]] reference operator*() const {
        if (empty()) {
            throw std::out_of_range("trust::IteratorRange::operator*: iterator is exhausted");
        }
        return *m_first;
    }

    [[nodiscard]] auto operator->() const
        requires requires(const It& i) { i.operator->(); }
    {
        if (empty()) {
            throw std::out_of_range("trust::IteratorRange::operator->: iterator is exhausted");
        }
        return m_first.operator->();
    }

    IteratorRange& operator++() {
        ++m_first;
        return *this;
    }

    IteratorRange operator++(int) {
        IteratorRange copy = *this;
        ++m_first;
        return copy;
    }

    IteratorRange& operator--()
        requires std::bidirectional_iterator<It>
    {
        --m_first;
        return *this;
    }

    IteratorRange operator--(int)
        requires std::bidirectional_iterator<It>
    {
        IteratorRange copy = *this;
        --m_first;
        return copy;
    }

    IteratorRange& operator+=(difference_type offset)
        requires std::random_access_iterator<It>
    {
        m_first += offset;
        return *this;
    }

    IteratorRange& operator-=(difference_type offset)
        requires std::random_access_iterator<It>
    {
        m_first -= offset;
        return *this;
    }

    /// Число оставшихся элементов (только для sized-sentinel).
    [[nodiscard]] difference_type size() const
        requires std::sized_sentinel_for<Sent, It>
    {
        return m_last - m_first;
    }

    /// Текущая позиция (escape hatch к нативному итератору).
    [[nodiscard]] It current() const { return m_first; }

    /// Конец последовательности (escape hatch к нативному sentinel).
    [[nodiscard]] const Sent& limit() const { return m_last; }

    /// Начать обход заново по новой последовательности.
    void reset(It first, Sent last) {
        m_first = std::move(first);
        m_last = std::move(last);
    }

    // -- Диапазон оставшихся элементов (range-for) --
    [[nodiscard]] It begin() const { return m_first; }
    [[nodiscard]] Sent end() const { return m_last; }

    friend bool operator==(const IteratorRange& lhs, const IteratorRange& rhs) { return lhs.m_first == rhs.m_first && lhs.m_last == rhs.m_last; }

  private:
    It m_first{};
    Sent m_last{};
};

template <std::input_iterator It, std::sentinel_for<It> Sent>
IteratorRange(It, Sent) -> IteratorRange<It, Sent>;

template <std::ranges::range R>
IteratorRange(R&&) -> IteratorRange<std::ranges::iterator_t<R>, std::ranges::sentinel_t<R>>;

/// Фактория: курсор из нативных итераторов.
template <std::input_iterator It, std::sentinel_for<It> Sent>
[[nodiscard]] IteratorRange<It, Sent> makeIteratorRange(It first, Sent last) {
    return IteratorRange<It, Sent>(std::move(first), std::move(last));
}

/// Фактория: курсор из диапазона/контейнера (borrowed view).
template <std::ranges::range R>
[[nodiscard]] auto makeIteratorRange(R&& range) {
    return IteratorRange<std::ranges::iterator_t<R>, std::ranges::sentinel_t<R>>(std::ranges::begin(range), std::ranges::end(range));
}

} // namespace trust
