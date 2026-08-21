// trust/range.hpp - universal arithmetic range for the Trust runtime.
//
// Public runtime header: self-contained (standard headers + rational.hpp + dict.hpp)
// so that generated C++ programs can include it without depending on the compiler's
// include tree. At build time it is embedded into trust-runtime.so/.a (via #embed, in
// an ELF section named "trust/range.hpp"); the pipeline extracts it into a temporary
// `trust/` directory when a program actually uses the Range type.
//
// Design:
//   - `Range<T>` is a *template* over the concrete element type T (like trust::Dict is
//     a universal heterogeneous container). T can be any arithmetic type: integral
//     (int8..int64, uint8..uint64), floating (float/double), trust::Rational, or the
//     universal `std::any` (Any). The compiler's codegen emits `trust::Range<Elem>`
//     where Elem is the joined type of the range operands (Int/Rational/Float/Any).
//   - Inclusive semantics (model: Kotlin `a..b`): `1..10` yields 1,2,...,10; the stop
//     value is included when reachable by the step. The default step is +1 when
//     start<=stop, otherwise -1 (for types that support it).
//   - Lazy by design: start()/stop()/step()/count()/at()/iterators compute on demand
//     and never materialize the whole sequence; toVector()/toArray()/toDict() materialize
//     on explicit request.
//   - Errors are reported with standard exceptions (std::out_of_range / std::bad_any_cast
//     / std::invalid_argument) so the header stays self-contained and usable without
//     linking the trust-runtime library.
//
// Cross-language API coverage: Python range(), Kotlin IntRange (step/downTo/reversed),
// Rust Range/RangeInclusive, C# Enumerable.Range, Swift/Ruby Range, Haskell [a..b].

#pragma once

#include <any>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "rational.hpp"
#include "dict.hpp"

namespace trust {

// -- detail: операции над элементом T (арифметика/сравнение), унифицированные для
//    интегральных/float/Rational и std::any (Any). Для std::any арифметика идёт через
//    double-представление (универсальный диапазон); для остальных T - напрямую. --
namespace detail {

/// Преобразование значения в double (для std::any-диапазона и подсчёта).
inline double rangeAsDouble(const std::any& v) {
    if (!v.has_value()) {
        return 0.0;
    }
    const auto& t = v.type();
    if (t == typeid(double)) {
        return std::any_cast<double>(v);
    }
    if (t == typeid(float)) {
        return std::any_cast<float>(v);
    }
    if (t == typeid(long double)) {
        return static_cast<double>(std::any_cast<long double>(v));
    }
    if (t == typeid(int64_t)) {
        return static_cast<double>(std::any_cast<int64_t>(v));
    }
    if (t == typeid(int32_t)) {
        return static_cast<double>(std::any_cast<int32_t>(v));
    }
    if (t == typeid(int16_t)) {
        return static_cast<double>(std::any_cast<int16_t>(v));
    }
    if (t == typeid(int8_t)) {
        return static_cast<double>(std::any_cast<int8_t>(v));
    }
    if (t == typeid(int)) {
        return static_cast<double>(std::any_cast<int>(v));
    }
    if (t == typeid(uint64_t)) {
        return static_cast<double>(std::any_cast<uint64_t>(v));
    }
    if (t == typeid(uint32_t)) {
        return static_cast<double>(std::any_cast<uint32_t>(v));
    }
    if (t == typeid(uint16_t)) {
        return static_cast<double>(std::any_cast<uint16_t>(v));
    }
    if (t == typeid(uint8_t)) {
        return static_cast<double>(std::any_cast<uint8_t>(v));
    }
    if (t == typeid(unsigned)) {
        return static_cast<double>(std::any_cast<unsigned>(v));
    }
    if (t == typeid(bool)) {
        return std::any_cast<bool>(v) ? 1.0 : 0.0;
    }
    if (t == typeid(trust::Rational)) {
        return std::any_cast<const trust::Rational&>(v).GetAsNumber();
    }
    throw std::bad_any_cast();
}

// Сравнение: приоритетная перегрузка для std::any, шаблонная - для остальных.
template <typename T>
constexpr bool rangeLess(const T& a, const T& b) {
    return a < b;
}
inline bool rangeLess(const std::any& a, const std::any& b) {
    return rangeAsDouble(a) < rangeAsDouble(b);
}

template <typename T>
constexpr bool rangeEqual(const T& a, const T& b) {
    return a == b;
}
inline bool rangeEqual(const std::any& a, const std::any& b) {
    return rangeAsDouble(a) == rangeAsDouble(b);
}

template <typename T>
constexpr bool rangeLessEqual(const T& a, const T& b) {
    return a <= b;
}
inline bool rangeLessEqual(const std::any& a, const std::any& b) {
    return rangeAsDouble(a) <= rangeAsDouble(b);
}

template <typename T>
constexpr bool rangeGreaterEqual(const T& a, const T& b) {
    return a >= b;
}
inline bool rangeGreaterEqual(const std::any& a, const std::any& b) {
    return rangeAsDouble(a) >= rangeAsDouble(b);
}

// Сложение start + i*step: для std::any - через double, иначе - напрямую.
template <typename T>
constexpr T rangeAdd(const T& a, const T& b) {
    return a + b;
}
inline std::any rangeAdd(const std::any& a, const std::any& b) {
    return std::any(rangeAsDouble(a) + rangeAsDouble(b));
}

// value * factor (для at(i): start + i*step).
template <typename T, typename F>
constexpr T rangeScale(const T& value, const F& factor) {
    return value * factor;
}
inline std::any rangeScale(const std::any& value, const std::any& factor) {
    return std::any(rangeAsDouble(value) * rangeAsDouble(factor));
}

// «Единица» в типе T (для default step).
template <typename T>
constexpr T rangeOne() {
    return T(1);
}

// «Умеет ли T отрицательный шаг»: знаковые целые, float, Rational - да; беззнаковые - нет.
template <typename T>
constexpr bool rangeCanNegStep = std::is_signed_v<T> || std::is_floating_point_v<T>;
template <>
inline constexpr bool rangeCanNegStep<std::any> = true;
template <>
inline constexpr bool rangeCanNegStep<trust::Rational> = true;

template <typename T>
constexpr T rangeNegOne() {
    return T(-1);
}

// TypeKind group codes (ABI: тот же формат, что trust::TypedValue / types/typekind.hpp:
// Group в битах 0-7, Data/размерность в битах 8-15). Самодостаточный заголовок не включает
// дерево компилятора, поэтому коды заданы локально (как в test/unit/runtime/dict_test.cpp).
inline constexpr uint32_t kRangeGroupLogical = 2u;
inline constexpr uint32_t kRangeGroupIntegers = 3u;
inline constexpr uint32_t kRangeGroupUnsigned = 4u;
inline constexpr uint32_t kRangeGroupNumbers = 5u;
inline constexpr uint32_t kRangeGroupRationals = 8u;

} // namespace detail

/// Диапазон `start..stop[..step]` над арифметическим типом T (Int/Float/Rational/Any).
/// Инклюзивная семантика (Kotlin-модель): stop включается, если достижим шагом.
/// Ленивый: значения вычисляются на лету (start/stop/step/count/at/iterator);
/// материализация - toVector()/toArray()/toDict().
template <typename T>
class Range {
  public:
    using value_type = T;

    /// start..stop с дефолтным шагом: +1 если start<=stop, иначе -1 (для T с отрицательным
    /// шагом); для беззнаковых T дефолтный шаг всегда +1 (обратный диапазон пуст).
    Range(T start, T stop)
    : m_start(std::move(start))
    , m_stop(std::move(stop)) {
        if constexpr (detail::rangeCanNegStep<T>) {
            m_step = detail::rangeGreaterEqual(m_stop, m_start) ? detail::rangeOne<T>() : detail::rangeNegOne<T>();
        } else {
            m_step = detail::rangeOne<T>();
        }
    }

    /// start..stop..step.
    Range(T start, T stop, T step)
    : m_start(std::move(start))
    , m_stop(std::move(stop))
    , m_step(std::move(step)) {
        if (detail::rangeEqual(m_step, T(0))) {
            throw std::invalid_argument("trust::Range: step must not be zero");
        }
    }

    /// -- Свойства --
    [[nodiscard]] const T& start() const noexcept { return m_start; }
    /// Конец диапазона (инклюзивный).
    [[nodiscard]] const T& stop() const noexcept { return m_stop; }
    [[nodiscard]] const T& step() const noexcept { return m_step; }

    /// Число элементов (вычислимо, без материализации).
    [[nodiscard]] std::size_t count() const {
        if constexpr (std::is_integral_v<T>) {
            return integralCount_();
        } else if constexpr (std::is_same_v<T, trust::Rational>) {
            return rationalCount_();
        } else {
            // float/double и std::any - через double-представление.
            return floatCount_();
        }
    }

    /// Алиас count() (как у контейнеров).
    [[nodiscard]] std::size_t size() const { return count(); }
    /// Диапазон пуст (нет ни одного элемента).
    [[nodiscard]] bool empty() const { return count() == 0; }

    /// true, если v входит в диапазон (start..stop и достижимо шагом).
    [[nodiscard]] bool contains(const T& v) const {
        if constexpr (std::is_integral_v<T>) {
            return integralContains_(v);
        } else if constexpr (std::is_same_v<T, trust::Rational>) {
            return rationalContains_(v);
        } else {
            return floatContains_(v);
        }
    }

    /// Элемент по индексу (0-базовый; вне [0, count) - std::out_of_range).
    [[nodiscard]] T at(std::size_t index) const {
        if (index >= count()) {
            throw std::out_of_range("trust::Range::at: index out of range");
        }
        return element_(index);
    }

    /// Элемент по индексу (без проверки границ; вне [0, count) - UB).
    [[nodiscard]] T operator[](std::size_t index) const { return element_(index); }

    /// Обратный диапазон: те же start/stop, шаг с обратным знаком (лениво).
    [[nodiscard]] Range reversed() const { return Range(m_stop, m_start, negate_(m_step)); }

    /// -- Итераторы (ленивые, forward) --
    class iterator {
      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = T;

        iterator() = default;
        iterator(const Range* owner, std::size_t index)
        : m_owner(owner)
        , m_index(index) {}

        T operator*() const { return m_owner->element_(m_index); }
        iterator& operator++() {
            ++m_index;
            return *this;
        }
        iterator operator++(int) {
            iterator tmp = *this;
            ++*this;
            return tmp;
        }
        friend bool operator==(const iterator& a, const iterator& b) { return a.m_owner == b.m_owner && a.m_index == b.m_index; }
        friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }

      private:
        const Range* m_owner = nullptr;
        std::size_t m_index = 0;
    };

    [[nodiscard]] iterator begin() const { return iterator(this, 0); }
    [[nodiscard]] iterator end() const { return iterator(this, count()); }
    [[nodiscard]] iterator cbegin() const { return begin(); }
    [[nodiscard]] iterator cend() const { return end(); }

    /// -- Материализация --
    /// Раскрытие в std::vector<T>.
    [[nodiscard]] std::vector<T> toVector() const {
        std::vector<T> out;
        out.reserve(count());
        for (std::size_t i = 0; i < count(); ++i) {
            out.push_back(element_(i));
        }
        return out;
    }
    /// Алиас toVector() (массив неизвестного размера → vector).
    [[nodiscard]] std::vector<T> toArray() const { return toVector(); }
    [[nodiscard]] std::vector<T> toList() const { return toVector(); }

    /// Преобразование в универсальный словарь trust::Dict: каждый элемент - (index, value)
    /// (имя = десятичный индекс, значение = TypedValue с естественным типом элемента).
    [[nodiscard]] Dict toDict() const {
        Dict out;
        const std::size_t n = count();
        for (std::size_t i = 0; i < n; ++i) {
            out.push_back(std::to_string(i), rangeTypedValue_(element_(i)));
        }
        return out;
    }

    /// -- Сравнение (по start/stop/step) --
    friend bool operator==(const Range& a, const Range& b) {
        return detail::rangeEqual(a.m_start, b.m_start) && detail::rangeEqual(a.m_stop, b.m_stop) && detail::rangeEqual(a.m_step, b.m_step);
    }
    friend bool operator!=(const Range& a, const Range& b) { return !(a == b); }

  private:
    static T negate_(const T& v) {
        if constexpr (std::is_same_v<T, std::any>) {
            return std::any(-detail::rangeAsDouble(v));
        } else {
            return -v;
        }
    }
    [[nodiscard]] T element_(std::size_t i) const {
        if constexpr (std::is_same_v<T, std::any>) {
            return detail::rangeAdd(m_start, detail::rangeScale(m_step, std::any(static_cast<double>(i))));
        } else {
            return detail::rangeAdd(m_start, detail::rangeScale(m_step, T(i)));
        }
    }
    [[nodiscard]] std::size_t integralCount_() const {
        const bool forward = (m_step > T(0));
        if (forward && m_start > m_stop) {
            return 0;
        }
        if (!forward && m_start < m_stop) {
            return 0;
        }
        if constexpr (std::is_signed_v<T>) {
            const long long diff = static_cast<long long>(m_stop) - static_cast<long long>(m_start);
            const long long s = static_cast<long long>(m_step);
            const long long n = diff / s;
            return n >= 0 ? static_cast<std::size_t>(n) + 1 : 0;
        } else {
            const unsigned long long diff = static_cast<unsigned long long>(m_stop) - static_cast<unsigned long long>(m_start);
            const unsigned long long s = static_cast<unsigned long long>(m_step);
            return static_cast<std::size_t>(diff / s) + 1;
        }
    }
    [[nodiscard]] bool integralContains_(const T& v) const {
        const bool forward = (m_step > T(0));
        if (forward && (v < m_start || v > m_stop)) {
            return false;
        }
        if (!forward && (v > m_start || v < m_stop)) {
            return false;
        }
        const long long d = static_cast<long long>(v) - static_cast<long long>(m_start);
        const long long s = static_cast<long long>(m_step);
        return s != 0 && (d % s == 0);
    }
    [[nodiscard]] std::size_t floatCount_() const {
        const double dstart = detail::rangeAsDouble(std::any(m_start));
        const double dstop = detail::rangeAsDouble(std::any(m_stop));
        const double dstep = detail::rangeAsDouble(std::any(m_step));
        if (dstep == 0.0) {
            return 0;
        }
        const double span = dstop - dstart;
        const double n = std::floor(span / dstep);
        return n >= 0.0 ? static_cast<std::size_t>(n) + 1 : 0;
    }
    [[nodiscard]] bool floatContains_(const T& v) const {
        const double dstart = detail::rangeAsDouble(std::any(m_start));
        const double dstop = detail::rangeAsDouble(std::any(m_stop));
        const double dstep = detail::rangeAsDouble(std::any(m_step));
        const double dv = detail::rangeAsDouble(std::any(v));
        if (dstep > 0.0 && (dv < dstart || dv > dstop)) {
            return false;
        }
        if (dstep < 0.0 && (dv > dstart || dv < dstop)) {
            return false;
        }
        if (dstep == 0.0) {
            return false;
        }
        const double q = (dv - dstart) / dstep;
        const double r = q - std::floor(q);
        return r < 1e-9;
    }
    [[nodiscard]] std::size_t rationalCount_() const {
        const trust::Rational zero(0);
        const bool forward = (m_step > zero);
        if (forward && m_start > m_stop) {
            return 0;
        }
        if (!forward && m_start < m_stop) {
            return 0;
        }
        const trust::Rational span = m_stop - m_start;
        const trust::Rational n = span / m_step; // точное деление рациональных
        const long long k = n.GetAsInteger();    // truncate к нулю; для forward = floor
        return k >= 0 ? static_cast<std::size_t>(k) + 1 : 0;
    }
    [[nodiscard]] bool rationalContains_(const T& v) const {
        const trust::Rational zero(0);
        const bool forward = (m_step > zero);
        if (forward && (v < m_start || v > m_stop)) {
            return false;
        }
        if (!forward && (v > m_start || v < m_stop)) {
            return false;
        }
        const trust::Rational d = (v - m_start) / m_step;
        return d.isInteger();
    }

    /// Элемент T → trust::TypedValue с естественным TypeKind (для toDict).
    static TypedValue rangeTypedValue_(const T& v) {
        if constexpr (std::is_same_v<T, trust::Rational>) {
            return TypedValue(detail::kRangeGroupRationals | (1u << 8), v);
        } else if constexpr (std::is_same_v<T, bool>) {
            return TypedValue(detail::kRangeGroupLogical | (1u << 8), v);
        } else if constexpr (std::is_floating_point_v<T>) {
            return TypedValue(detail::kRangeGroupNumbers | (sizeof(T) * 8u << 8), v);
        } else if constexpr (std::is_unsigned_v<T>) {
            return TypedValue(detail::kRangeGroupUnsigned | (sizeof(T) * 8u << 8), v);
        } else if constexpr (std::is_integral_v<T>) {
            return TypedValue(detail::kRangeGroupIntegers | (sizeof(T) * 8u << 8), v);
        } else {
            // std::any и прочие - Any (kind=0, значение в std::any-ветке).
            return TypedValue(0, v);
        }
    }

    T m_start;
    T m_stop;
    T m_step;
};

} // namespace trust

// std::format support: Range formats as `start..stop[..step]`.
template <typename T>
struct std::formatter<trust::Range<T>> : std::formatter<std::string> {
    auto format(const trust::Range<T>& r, std::format_context& ctx) const {
        std::string s;
        if constexpr (std::is_same_v<T, std::any>) {
            s = "any..any";
        } else if constexpr (std::is_same_v<T, trust::Rational>) {
            s = r.start().GetAsString() + ".." + r.stop().GetAsString();
        } else {
            s = std::to_string(r.start()) + ".." + std::to_string(r.stop());
        }
        return std::formatter<std::string>::format(s, ctx);
    }
};
