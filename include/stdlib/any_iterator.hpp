// stdlib/any_iterator.hpp - type-erased universal iterator (single-pass cursor) for the Trust runtime.
//
// Standard library header of a library type (stdlib asset, "stdlib/any_iterator.hpp"):
// self-contained (standard headers only). Embedded into the COMPILER (src/assets/asset_provider.cpp)
// and extracted into a temporary `stdlib/` directory when a generated program uses the Iterator
// type in its erased (uniform) representation.
//
// Design:
//   - `AnyIterator<Value>` has ONE concrete type over ANY underlying [first, last) sequence
//     (heterogeneous sources, module/ABI boundaries). The concrete iterator type is erased.
//   - Java/Rust/Python cursor model: `hasNext()` / `next()`. The end sentinel is stored inside,
//     so the completion check needs no access to the source container.
//   - Single-pass and move-only (no `clone`): copying a cursor would alias one position.
//   - `next()` / `operator*` / `operator++` on an exhausted cursor throw std::out_of_range
//     (no silent fallback), consistent with trust::Range::at.

#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace trust {

/// Type-erased, single-pass cursor producing `Value`.
template <typename Value>
class AnyIterator {
  public:
    using value_type = Value;
    using reference = Value;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;

    /// Пустой (исчерпанный) курсор.
    AnyIterator() noexcept = default;

    /// Из нативных C++-итераторов [first, last).
    template <std::input_iterator It, std::sentinel_for<It> Sent>
        requires std::convertible_to<std::iter_reference_t<It>, Value>
    AnyIterator(It first, Sent last)
    : m_impl(std::make_unique<Model<It, Sent>>(std::move(first), std::move(last))) {}

    /// Из диапазона/контейнера (borrowed view: НЕ владеет данными).
    template <std::ranges::input_range R>
        requires(!std::same_as<std::remove_cvref_t<R>, AnyIterator> && std::convertible_to<std::ranges::range_reference_t<R>, Value>)
    explicit AnyIterator(R&& range)
    : AnyIterator(std::ranges::begin(range), std::ranges::end(range)) {}

    /// Есть ли ещё элементы (конец хранится внутри).
    [[nodiscard]] bool hasNext() const { return m_impl != nullptr && !m_impl->atEnd(); }

    /// Следующий элемент (потребляющий шаг). Исчерпанный курсор → std::out_of_range.
    [[nodiscard]] Value next() {
        if (!hasNext()) {
            throw std::out_of_range("trust::AnyIterator::next: iterator is exhausted");
        }
        Value value = m_impl->get();
        m_impl->advance();
        return value;
    }

    /// Текущий элемент без продвижения. Исчерпанный курсор → std::out_of_range.
    [[nodiscard]] Value operator*() const {
        if (!hasNext()) {
            throw std::out_of_range("trust::AnyIterator::operator*: iterator is exhausted");
        }
        return m_impl->get();
    }

    AnyIterator& operator++() {
        if (!hasNext()) {
            throw std::out_of_range("trust::AnyIterator::operator++: iterator is exhausted");
        }
        m_impl->advance();
        return *this;
    }

    void operator++(int) { ++(*this); }

    /// Явное продвижение (алиас `++`), удобно для детерминированного маппинга trust-метода.
    void advance() { ++(*this); }

    [[nodiscard]] explicit operator bool() const { return hasNext(); }

    friend bool operator==(const AnyIterator& it, std::default_sentinel_t) { return !it.hasNext(); }
    friend bool operator==(std::default_sentinel_t, const AnyIterator& it) { return !it.hasNext(); }

  private:
    /// Type-erasure interface: pure virtual dispatch over the concrete iterator pair.
    struct Concept {
        Concept() = default;
        Concept(const Concept&) = delete;
        Concept& operator=(const Concept&) = delete;
        virtual ~Concept() = default;
        [[nodiscard]] virtual Value get() const = 0;
        virtual void advance() = 0;
        [[nodiscard]] virtual bool atEnd() const = 0;
    };

    template <std::input_iterator It, std::sentinel_for<It> Sent>
    struct Model final : Concept {
        It m_first;
        Sent m_last;

        Model(It first, Sent last)
        : m_first(std::move(first))
        , m_last(std::move(last)) {}

        [[nodiscard]] Value get() const override { return *m_first; }
        void advance() override { ++m_first; }
        [[nodiscard]] bool atEnd() const override { return m_first == m_last; }
    };

    std::unique_ptr<Concept> m_impl;
};

/// Фактория: устранённый курсор из нативных итераторов.
template <typename Value, std::input_iterator It, std::sentinel_for<It> Sent>
[[nodiscard]] AnyIterator<Value> makeAnyIterator(It first, Sent last) {
    return AnyIterator<Value>(std::move(first), std::move(last));
}

/// Фактория: устранённый курсор из диапазона/контейнера.
template <typename Value, std::ranges::input_range R>
[[nodiscard]] AnyIterator<Value> makeAnyIterator(R&& range) {
    return AnyIterator<Value>(range);
}

/// Фактория с выводом Value из типа элемента диапазона (для нативных вызовов без явных <T>).
template <std::ranges::input_range R>
[[nodiscard]] AnyIterator<std::ranges::range_value_t<R>> makeAnyIterator(R&& range) {
    return AnyIterator<std::ranges::range_value_t<R>>(range);
}

} // namespace trust
