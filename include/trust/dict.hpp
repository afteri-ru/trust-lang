// trust/dict.hpp - universal heterogeneous dictionary for the Trust runtime.
//
// Public runtime header: self-contained (standard headers only) so that generated
// C++ programs can include it without depending on the compiler's include tree.
// At build time it is embedded into trust-runtime.so/.a (via #embed, in an ELF
// section named "trust/dict.hpp"); the pipeline extracts it into a temporary
// `trust/` directory when a program actually uses the Dict type.
//
// Design notes:
//   - Dict is a *universal* heterogeneous 1-D container (like a tuple + struct
//     at once): each element is a (name, value) pair, where value is TypedValue
//     (kind + value). The value uses a std::variant fast-path for known categories
//     (numbers/bool/strings/Rational) plus a std::any fallback, so an element can hold a
//     value of any type, including a nested Dict.
//   - Elements are accessed by integer index (0-based; negative = from the end)
//     or by name when present (empty name = unnamed positional element).
//   - It is deliberately not the fastest container - the goal is maximum
//     universality and easy conversion to/from any concrete data type.
//   - Errors are reported with standard exceptions (std::out_of_range /
//     std::bad_any_cast) so the header stays self-contained and usable without
//     linking the trust-runtime library.

#pragma once

#include <any>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

// Rational (pimpl, полный тип без gmp.h) - встраивается по значению в быструю ветку variant.
#include "rational.hpp"

namespace trust {

// Логический тип TrustLang - TypeKind (кодировка Group(0-7)|Data(8-15)|…), тот же тип-алиас,
// что и в компиляторе (types/typekind.hpp: `using TypeKind = uint32_t`). Рантайм-заголовок
// самодостаточен и не включает дерево компилятора, поэтому алиас объявлен здесь (один и тот же
// формат). Поле kind объявлено через TypeKind, а не голый uint32_t, чтобы тип-намерение был
// яdiог эволюционировать.
using TypeKind = uint32_t;

// Предварительное объявление хелпера типизированного доступа (определяется в namespace detail
// ниже, в разделе inline-helper'ов dict.hpp): нужен для TypedValue::getAs<T>().
struct TypedValue;
namespace detail {
template <typename T>
[[nodiscard]] T typedGet(const TypedValue& tv);
} // namespace detail

/// Типизированное значение элемента словаря: само значение + kind - TypeKind (логический
/// тип TrustLang, закодирован как Group|Data).
///
/// Значение хранится в std::variant из ОБОБЩЁННЫХ C++-типов (быстрая ветка для заранее
/// известных категорий: числа/bool/строки/Rational) + std::any (открытые/реестровые типы, в т.ч.
/// вложенный Dict - из-за круговой зависимости TypedValue↔Dict по значению). Rational хранится
/// ПО ЗНАЧЕНИЮ (pimpl, полный тип без gmp.h); копирование - внутренний deep-copy, без разделения.
/// Точный тип и размерность всегда в kind (Group|Data); storage хранит только обобщение:
/// int8..int64 → int64_t, uint8..uint64 → uint64_t, float/double → double. Такой доступ не требует
/// typeid-каскада: ветка выбирается по группе (std::get), размерность кастуется static_cast по
/// data() из kind. Декодирование kind - автономно, без TypeRegistry: кодировка самодостаточна.
struct TypedValue {
    TypeKind kind = 0; ///< TypeKind: Group(0-7) | Data/размерность(8-15).
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, std::wstring, std::any, Rational>
        storage; ///< Значение: быстрая ветка (обобщённый тип / Rational) либо std::any (прочие/открытые).

    TypedValue() = default;

    /// Конструирует значение с типом kind. В быструю ветку variant кладёт значение, если его
    /// C++-тип соответствует группе kind (число → обобщение, строка → строка, bool → bool);
    /// иначе - в std::any (в т.ч. вложенный Dict и реестровые/открытые типы).
    template <typename T>
    TypedValue(TypeKind k, T&& v)
    : kind(k) {
        store(std::forward<T>(v));
    }

    /// -- Декодирование TypeKind (биты, как в types/typekind.hpp) --
    /// Группа (логическая категория), биты 0-7.
    [[nodiscard]] uint8_t group() const noexcept { return static_cast<uint8_t>(kind & 0xFFu); }
    /// Data - размерность в битах / код, биты 8-15.
    [[nodiscard]] uint8_t data() const noexcept { return static_cast<uint8_t>((kind >> 8) & 0xFFu); }

    [[nodiscard]] bool isBool() const noexcept { return group() == kGroupLogical; }
    [[nodiscard]] bool isInteger() const noexcept { return group() == kGroupIntegers; }
    [[nodiscard]] bool isUnsigned() const noexcept { return group() == kGroupUnsigned; }
    [[nodiscard]] bool isFloat() const noexcept { return group() == kGroupNumbers; }
    [[nodiscard]] bool isNumeric() const noexcept { return isInteger() || isUnsigned() || isFloat(); }
    [[nodiscard]] bool isStrChar() const noexcept { return group() == kGroupStrChar; }
    [[nodiscard]] bool isStrWide() const noexcept { return group() == kGroupStrWide; }
    [[nodiscard]] bool isString() const noexcept { return isStrChar() || isStrWide(); }
    [[nodiscard]] bool isDict() const noexcept { return group() == kGroupDicts; }
    [[nodiscard]] bool isRational() const noexcept { return group() == kGroupRationals; }

    /// Типизированный доступ к значению по C++-типу T (быстрая ветка variant или std::any).
    /// Несовпадение категории T с группой значения → std::bad_any_cast.
    template <typename T>
    [[nodiscard]] T getAs() const {
        return detail::typedGet<T>(*this);
    }

    /// Значение как std::any его естественного C++-типа (быстрая ветка variant или вложенный
    /// std::any). Нужен деструктуризации (`item, dict := ... dict`): item типизируется std::any,
    /// а арифметика над ним идёт через runtime-конвертеры (anyToInt64 и т.п.).
    [[nodiscard]] std::any toAny() const {
        return std::visit(
            [](const auto& v) -> std::any {
                using DT = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<DT, std::monostate>) {
                    return std::any{};
                } else if constexpr (std::is_same_v<DT, std::any>) {
                    return v;
                } else {
                    return std::any(v);
                }
            },
            storage);
    }

  private:
    /// Размещение значения в соответствующей ветке variant по group() и C++-типу.
    template <typename T>
    void store(T&& v) {
        using DT = std::decay_t<T>;
        const uint8_t g = group();
        if constexpr (std::is_same_v<DT, bool>) {
            if (g == kGroupLogical) {
                storage = static_cast<bool>(std::forward<T>(v));
                return;
            }
        } else if constexpr (std::is_integral_v<DT> && std::is_signed_v<DT>) {
            if (g == kGroupIntegers) {
                storage = static_cast<std::int64_t>(std::forward<T>(v));
                return;
            }
            if (g == kGroupUnsigned) {
                storage = static_cast<std::uint64_t>(std::forward<T>(v));
                return;
            }
        } else if constexpr (std::is_integral_v<DT> && std::is_unsigned_v<DT>) {
            if (g == kGroupUnsigned) {
                storage = static_cast<std::uint64_t>(std::forward<T>(v));
                return;
            }
            if (g == kGroupIntegers) {
                storage = static_cast<std::int64_t>(std::forward<T>(v));
                return;
            }
        } else if constexpr (std::is_floating_point_v<DT>) {
            if (g == kGroupNumbers) {
                storage = static_cast<double>(std::forward<T>(v));
                return;
            }
        } else if constexpr (std::is_same_v<DT, std::string>) {
            if (g == kGroupStrChar) {
                storage = std::forward<T>(v);
                return;
            }
        } else if constexpr (std::is_same_v<DT, std::wstring>) {
            if (g == kGroupStrWide) {
                storage = std::forward<T>(v);
                return;
            }
        } else if constexpr (std::is_same_v<DT, Rational>) {
            if (g == kGroupRationals) {
                storage = std::forward<T>(v); // по значению; копирование - внутренний deep-copy Rational
                return;
            }
        }
        storage = std::any(std::forward<T>(v));
    }

  private:
    // Значения Group из types/group.hpp - кодировка TypeKind (ABI рантайма).
    static constexpr uint8_t kGroupLogical = 2;
    static constexpr uint8_t kGroupIntegers = 3;
    static constexpr uint8_t kGroupUnsigned = 4;
    static constexpr uint8_t kGroupNumbers = 5;
    static constexpr uint8_t kGroupStrChar = 9;
    static constexpr uint8_t kGroupStrWide = 10;
    static constexpr uint8_t kGroupDicts = 11;
    static constexpr uint8_t kGroupRationals = 8;
};

class Dict {
  public:
    using value_type = TypedValue;
    using element_type = std::pair<std::string, value_type>;
    using container_type = std::vector<element_type>;

    Dict() = default;

    /// Создание из brace-списка пар {name, TypedValue} (кодогенерация литерала словаря).
    /// Имя пустой строки = безымянный (позиционный) элемент.
    Dict(std::initializer_list<element_type> items)
    : m_items(items) {}

    /// Доступ по целочисленному индексу (0-базовый; отрицательный - с конца).
    value_type& at(int64_t index);
    const value_type& at(int64_t index) const;

    /// Доступ по имени элемента.
    value_type& at(std::string_view name);
    const value_type& at(std::string_view name) const;

    value_type& operator[](int64_t index) { return at(index); }
    const value_type& operator[](int64_t index) const { return at(index); }
    value_type& operator[](std::string_view name) { return at(name); }
    const value_type& operator[](std::string_view name) const { return at(name); }

    [[nodiscard]] size_t size() const noexcept { return m_items.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_items.empty(); }

    /// Истинность словаря: непустой словарь «истинен» (контекст `@while(dict)`/if). Explicit,
    /// чтобы избежать случайных неявных конверсий в арифметике.
    [[nodiscard]] explicit operator bool() const noexcept { return !m_items.empty(); }

    /// Добавление элемента (имя может быть пустым - позиционный элемент).
    void push_back(std::string name, value_type value) { m_items.emplace_back(std::move(name), std::move(value)); }

    /// Слияние: добор всех элементов другого словаря (копия пар «имя, значение»).
    /// Аналог `extend`/`update`/spread: `d []= ... d2` → `d.extend(d2)`. Безопасно для
    /// self-append (`d []= ... d`): при `this == &other` сначала копируем источник.
    void extend(const Dict& other) {
        if (this == &other) {
            auto copy = other.m_items;
            m_items.insert(m_items.end(), copy.begin(), copy.end());
        } else {
            m_items.insert(m_items.end(), other.m_items.begin(), other.m_items.end());
        }
    }

    void clear() noexcept { m_items.clear(); }

    /// Извлечение и удаление первого элемента (`item, dict := ... dict`). Возвращает значение
    /// первого элемента как std::any его естественного типа (имя отбрасывается) и удаляет его
    /// из словаря. Пустой словарь → out_of_range.
    std::any pop_front() {
        if (m_items.empty()) {
            throw std::out_of_range("trust::Dict::pop_front: dict is empty");
        }
        auto first = std::move(m_items.front().second).toAny();
        m_items.erase(m_items.begin());
        return first;
    }

    /// Индекс элемента по имени (или -1, если имени нет).
    [[nodiscard]] int64_t index(std::string_view name) const noexcept;
    [[nodiscard]] bool contains(std::string_view name) const noexcept;

    /// Универсальные конвертеры: извлечение значения нужного типа.
    /// Несовместимый тип → std::bad_any_cast.
    template <typename T>
    [[nodiscard]] T getAs(int64_t index) const {
        return at(index).getAs<T>();
    }
    template <typename T>
    [[nodiscard]] T getAs(std::string_view name) const {
        return at(name).getAs<T>();
    }

    /// Универсальное приведение словаря к простому типу: берётся первый элемент.
    /// (По аналогии с trust::Rational::GetAsInteger/GetAsNumber/GetAsString.)
    [[nodiscard]] int64_t GetAsInteger() const;
    [[nodiscard]] int64_t GetAsBoolean() const;
    [[nodiscard]] double GetAsNumber() const;
    [[nodiscard]] std::string GetAsString() const;

    bool operator==(const Dict& other) const;
    bool operator!=(const Dict& other) const { return !(*this == other); }

    // Итерация (для for-перебора и std::formatter).
    container_type::iterator begin() noexcept { return m_items.begin(); }
    container_type::iterator end() noexcept { return m_items.end(); }
    container_type::const_iterator begin() const noexcept { return m_items.begin(); }
    container_type::const_iterator end() const noexcept { return m_items.end(); }

  private:
    /// Нормализация индекса: отрицательный → с конца, выход за границы → std::out_of_range.
    static size_t normalize_index(int64_t index, size_t size) {
        int64_t n = static_cast<int64_t>(size);
        if (index < 0) {
            index += n;
        }
        if (index < 0 || index >= n) {
            throw std::out_of_range("trust::Dict: index out of range");
        }
        return static_cast<size_t>(index);
    }

    container_type m_items;
};

// -- inline implementations ------------------------------------------------

inline TypedValue& Dict::at(int64_t index) {
    return m_items[normalize_index(index, m_items.size())].second;
}

inline const TypedValue& Dict::at(int64_t index) const {
    return m_items[normalize_index(index, m_items.size())].second;
}

inline TypedValue& Dict::at(std::string_view name) {
    for (auto& e : m_items) {
        if (e.first == name) {
            return e.second;
        }
    }
    throw std::out_of_range("trust::Dict: key not found: '" + std::string(name) + "'");
}

inline const TypedValue& Dict::at(std::string_view name) const {
    for (const auto& e : m_items) {
        if (e.first == name) {
            return e.second;
        }
    }
    throw std::out_of_range("trust::Dict: key not found: '" + std::string(name) + "'");
}

inline int64_t Dict::index(std::string_view name) const noexcept {
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].first == name) {
            return static_cast<int64_t>(i);
        }
    }
    return -1;
}

inline bool Dict::contains(std::string_view name) const noexcept {
    return index(name) >= 0;
}

namespace detail {
/// Универсальное приведение std::any к int64_t (числа, bool, строка).
[[nodiscard]] inline int64_t anyToInt64(const std::any& v) {
    // Все ширины целых, которые может хранить Dict (Int8..Int64, unsigned).
    if (v.type() == typeid(int)) {
        return std::any_cast<int>(v);
    }
    if (v.type() == typeid(int8_t)) {
        return std::any_cast<int8_t>(v);
    }
    if (v.type() == typeid(int16_t)) {
        return std::any_cast<int16_t>(v);
    }
    if (v.type() == typeid(int32_t)) {
        return std::any_cast<int32_t>(v);
    }
    if (v.type() == typeid(int64_t)) {
        return std::any_cast<int64_t>(v);
    }
    if (v.type() == typeid(unsigned char)) {
        return std::any_cast<unsigned char>(v);
    }
    if (v.type() == typeid(uint16_t)) {
        return std::any_cast<uint16_t>(v);
    }
    if (v.type() == typeid(uint32_t)) {
        return std::any_cast<uint32_t>(v);
    }
    if (v.type() == typeid(uint64_t)) {
        return static_cast<int64_t>(std::any_cast<uint64_t>(v));
    }
    if (v.type() == typeid(bool)) {
        return std::any_cast<bool>(v) ? 1 : 0;
    }
    if (v.type() == typeid(double)) {
        return static_cast<int64_t>(std::any_cast<double>(v));
    }
    if (v.type() == typeid(std::string)) {
        const std::string& s = std::any_cast<const std::string&>(v);
        std::size_t pos = 0;
        long long r = std::stoll(s, &pos);
        if (pos != s.size()) {
            throw std::invalid_argument("trust::Dict: string is not an integer: '" + s + "'");
        }
        return static_cast<int64_t>(r);
    }
    return std::any_cast<int64_t>(v); // несовместимый тип → std::bad_any_cast
}

/// Универсальное приведение std::any к double (числа, bool, строка).
[[nodiscard]] inline double anyToDouble(const std::any& v) {
    if (v.type() == typeid(double)) {
        return std::any_cast<double>(v);
    }
    if (v.type() == typeid(float)) {
        return static_cast<double>(std::any_cast<float>(v));
    }
    if (v.type() == typeid(int)) {
        return static_cast<double>(std::any_cast<int>(v));
    }
    if (v.type() == typeid(int8_t)) {
        return static_cast<double>(std::any_cast<int8_t>(v));
    }
    if (v.type() == typeid(int16_t)) {
        return static_cast<double>(std::any_cast<int16_t>(v));
    }
    if (v.type() == typeid(int32_t)) {
        return static_cast<double>(std::any_cast<int32_t>(v));
    }
    if (v.type() == typeid(int64_t)) {
        return static_cast<double>(std::any_cast<int64_t>(v));
    }
    if (v.type() == typeid(unsigned char)) {
        return static_cast<double>(std::any_cast<unsigned char>(v));
    }
    if (v.type() == typeid(uint16_t)) {
        return static_cast<double>(std::any_cast<uint16_t>(v));
    }
    if (v.type() == typeid(uint32_t)) {
        return static_cast<double>(std::any_cast<uint32_t>(v));
    }
    if (v.type() == typeid(uint64_t)) {
        return static_cast<double>(std::any_cast<uint64_t>(v));
    }
    if (v.type() == typeid(bool)) {
        return std::any_cast<bool>(v) ? 1.0 : 0.0;
    }
    if (v.type() == typeid(std::string)) {
        return std::stod(std::any_cast<const std::string&>(v));
    }
    return std::any_cast<double>(v); // несовместимый тип → std::bad_any_cast
}

/// Универсальное приведение std::any к std::string (строка, числа, bool).
[[nodiscard]] inline std::string anyToString(const std::any& v) {
    if (v.type() == typeid(std::string)) {
        return std::any_cast<std::string>(v);
    }
    if (v.type() == typeid(const char*)) {
        return std::any_cast<const char*>(v);
    }
    return std::to_string(anyToDouble(v));
}

/// -- Типизированный доступ по TypeKind (быстрая ветка std::variant) --
/// Ветка выбирается по группе (std::get_if), без typeid-каскада; размерность кастуется по
/// data() из kind. std::any-ветка (открытые/реестровые типы) - прежний anyTo* путь.

/// Приведение TypedValue к int64_t по группе (числа/bool/строки).
[[nodiscard]] inline int64_t typedToInt64(const TypedValue& tv) {
    if (tv.isBool()) {
        if (const bool* p = std::get_if<bool>(&tv.storage)) {
            return *p ? 1 : 0;
        }
    } else if (tv.isInteger()) {
        if (const int64_t* p = std::get_if<int64_t>(&tv.storage)) {
            return *p;
        }
    } else if (tv.isUnsigned()) {
        if (const uint64_t* p = std::get_if<uint64_t>(&tv.storage)) {
            return static_cast<int64_t>(*p);
        }
    } else if (tv.isFloat()) {
        if (const double* p = std::get_if<double>(&tv.storage)) {
            return static_cast<int64_t>(*p);
        }
    } else if (tv.isStrChar()) {
        if (const std::string* p = std::get_if<std::string>(&tv.storage)) {
            std::size_t pos = 0;
            long long r = std::stoll(*p, &pos);
            if (pos != p->size()) {
                throw std::invalid_argument("trust::Dict: string is not an integer");
            }
            return static_cast<int64_t>(r);
        }
    } else if (tv.isStrWide()) {
        if (const std::wstring* p = std::get_if<std::wstring>(&tv.storage)) {
            const std::string narrow(p->begin(), p->end());
            std::size_t pos = 0;
            long long r = std::stoll(narrow, &pos);
            if (pos != narrow.size()) {
                throw std::invalid_argument("trust::Dict: string is not an integer");
            }
            return static_cast<int64_t>(r);
        }
    } else if (tv.isRational()) {
        if (const Rational* p = std::get_if<Rational>(&tv.storage)) {
            return p->GetAsInteger();
        }
    }
    // std::any-ветка (несоответствие типа при конструировании) - прежний anyToInt64.
    if (const std::any* a = std::get_if<std::any>(&tv.storage)) {
        return anyToInt64(*a);
    }
    return 0;
}

/// Приведение TypedValue к double по группе (числа/bool/строки).
[[nodiscard]] inline double typedToDouble(const TypedValue& tv) {
    if (tv.isBool()) {
        if (const bool* p = std::get_if<bool>(&tv.storage)) {
            return *p ? 1.0 : 0.0;
        }
    } else if (tv.isInteger()) {
        if (const int64_t* p = std::get_if<int64_t>(&tv.storage)) {
            return static_cast<double>(*p);
        }
    } else if (tv.isUnsigned()) {
        if (const uint64_t* p = std::get_if<uint64_t>(&tv.storage)) {
            return static_cast<double>(*p);
        }
    } else if (tv.isFloat()) {
        if (const double* p = std::get_if<double>(&tv.storage)) {
            return *p;
        }
    } else if (tv.isStrChar()) {
        if (const std::string* p = std::get_if<std::string>(&tv.storage)) {
            return std::stod(*p);
        }
    } else if (tv.isStrWide()) {
        if (const std::wstring* p = std::get_if<std::wstring>(&tv.storage)) {
            return std::stod(std::string(p->begin(), p->end()));
        }
    } else if (tv.isRational()) {
        if (const Rational* p = std::get_if<Rational>(&tv.storage)) {
            return p->GetAsNumber();
        }
    }
    if (const std::any* a = std::get_if<std::any>(&tv.storage)) {
        return anyToDouble(*a);
    }
    return 0.0;
}

/// Приведение TypedValue к std::string по группе (строки; числа → anyToString).
[[nodiscard]] inline std::string typedToStr(const TypedValue& tv) {
    if (const std::string* p = std::get_if<std::string>(&tv.storage)) {
        return *p;
    }
    if (const std::wstring* p = std::get_if<std::wstring>(&tv.storage)) {
        return std::string(p->begin(), p->end());
    }
    if (const Rational* p = std::get_if<Rational>(&tv.storage)) {
        return p->GetAsString();
    }
    if (const std::any* a = std::get_if<std::any>(&tv.storage)) {
        return anyToString(*a);
    }
    return "";
}

/// Сравнение двух std::any по значению (std::any не имеет operator==).
/// Для типов, не перечисленных ниже, значения считаются равными только при
/// совпадении type() и отсутствии значения. Универсальность - важнее полноты.
[[nodiscard]] inline bool anyEqual(const std::any& a, const std::any& b) {
    if (a.type() != b.type()) {
        return false;
    }
    if (a.has_value() != b.has_value()) {
        return false;
    }
    if (!a.has_value()) {
        return true;
    }
    const std::type_info& t = a.type();
    if (t == typeid(int)) {
        return std::any_cast<int>(a) == std::any_cast<int>(b);
    }
    if (t == typeid(int64_t)) {
        return std::any_cast<int64_t>(a) == std::any_cast<int64_t>(b);
    }
    if (t == typeid(uint64_t)) {
        return std::any_cast<uint64_t>(a) == std::any_cast<uint64_t>(b);
    }
    if (t == typeid(double)) {
        return std::any_cast<double>(a) == std::any_cast<double>(b);
    }
    if (t == typeid(bool)) {
        return std::any_cast<bool>(a) == std::any_cast<bool>(b);
    }
    if (t == typeid(std::string)) {
        return std::any_cast<std::string>(a) == std::any_cast<std::string>(b);
    }
    if (t == typeid(const char*)) {
        return std::string(std::any_cast<const char*>(a)) == std::string(std::any_cast<const char*>(b));
    }
    if (t == typeid(Dict)) {
        return std::any_cast<Dict>(a) == std::any_cast<Dict>(b);
    }
    return false; // неизвестный тип - консервативно: не равны
}

/// Сравнение двух TypedValue по значению: сравнение хранимой ветки variant напрямую
/// (числа/bool/строки - без typeid) либо std::any через anyEqual. kind сравнивается отдельно
/// в Dict::operator==; здесь - только значения.
[[nodiscard]] inline bool typedEqual(const TypedValue& a, const TypedValue& b) {
    if (const bool* x = std::get_if<bool>(&a.storage)) {
        const bool* y = std::get_if<bool>(&b.storage);
        return y && *x == *y;
    }
    if (const int64_t* x = std::get_if<int64_t>(&a.storage)) {
        const int64_t* y = std::get_if<int64_t>(&b.storage);
        return y && *x == *y;
    }
    if (const uint64_t* x = std::get_if<uint64_t>(&a.storage)) {
        const uint64_t* y = std::get_if<uint64_t>(&b.storage);
        return y && *x == *y;
    }
    if (const double* x = std::get_if<double>(&a.storage)) {
        const double* y = std::get_if<double>(&b.storage);
        return y && *x == *y;
    }
    if (const std::string* x = std::get_if<std::string>(&a.storage)) {
        const std::string* y = std::get_if<std::string>(&b.storage);
        return y && *x == *y;
    }
    if (const std::wstring* x = std::get_if<std::wstring>(&a.storage)) {
        const std::wstring* y = std::get_if<std::wstring>(&b.storage);
        return y && *x == *y;
    }
    if (const Rational* x = std::get_if<Rational>(&a.storage)) {
        const Rational* y = std::get_if<Rational>(&b.storage);
        return y && x->op_equal(*y);
    }
    const std::any* x = std::get_if<std::any>(&a.storage);
    const std::any* y = std::get_if<std::any>(&b.storage);
    if (x && y) {
        return anyEqual(*x, *y);
    }
    return false;
}

/// Типизированный доступ к значению TypedValue по C++-типу T. Для быстрой ветки variant
/// выбирается альтернатива по категории T (без typeid); для прочих типов - std::any_cast из
/// std::any-ветки. Несовпадение категории T с хранимой веткой → std::bad_any_cast.
template <typename T>
[[nodiscard]] inline T typedGet(const TypedValue& tv) {
    using DT = std::decay_t<T>;
    if constexpr (std::is_same_v<DT, bool>) {
        if (const bool* p = std::get_if<bool>(&tv.storage)) {
            return *p;
        }
        throw std::bad_any_cast();
    } else if constexpr (std::is_integral_v<DT> && std::is_signed_v<DT>) {
        if (const int64_t* p = std::get_if<int64_t>(&tv.storage)) {
            return static_cast<T>(*p);
        }
        throw std::bad_any_cast();
    } else if constexpr (std::is_integral_v<DT> && std::is_unsigned_v<DT>) {
        if (const uint64_t* p = std::get_if<uint64_t>(&tv.storage)) {
            return static_cast<T>(*p);
        }
        throw std::bad_any_cast();
    } else if constexpr (std::is_floating_point_v<DT>) {
        if (const double* p = std::get_if<double>(&tv.storage)) {
            return static_cast<T>(*p);
        }
        throw std::bad_any_cast();
    } else if constexpr (std::is_same_v<DT, std::string>) {
        if (const std::string* p = std::get_if<std::string>(&tv.storage)) {
            return *p;
        }
        throw std::bad_any_cast();
    } else if constexpr (std::is_same_v<DT, std::wstring>) {
        if (const std::wstring* p = std::get_if<std::wstring>(&tv.storage)) {
            return *p;
        }
        throw std::bad_any_cast();
    } else if constexpr (std::is_same_v<DT, Rational>) {
        if (const Rational* p = std::get_if<Rational>(&tv.storage)) {
            return *p;
        }
        throw std::bad_any_cast();
    } else {
        return std::any_cast<T>(std::get<std::any>(tv.storage));
    }
}

/// Форматирование значения TypedValue в строку по его TypeKind (группе): bool → "true"/"false",
/// целые/беззнаковые → число, числа с плавающей точкой → число, строки → строка, вложенный
/// словарь → "(имя=значение, ...)". Взаимно-рекурсивно с dictToString.
[[nodiscard]] inline std::string typedValueToString(const TypedValue& tv);

/// Форматирование Dict в строку в стиле литерала словаря: "(имя=значение, ...)". Форма
/// совпадает с литералом: каждый элемент завершается запятой, включая последний ("(a=1, b=2,)").
[[nodiscard]] inline std::string dictToString(const Dict& d) {
    std::string out = "(";
    bool first = true;
    for (const auto& [name, tv] : d) {
        if (!first) {
            out += ", ";
        }
        first = false;
        if (!name.empty()) {
            out += name;
            out += "=";
        }
        out += typedValueToString(tv);
    }
    if (!first) {
        out += ","; // завершающая запятая (литеральная форма словаря)
    }
    out += ")";
    return out;
}

[[nodiscard]] inline std::string typedValueToString(const TypedValue& tv) {
    if (tv.isBool()) {
        return typedToInt64(tv) ? "true" : "false";
    }
    if (tv.isInteger() || tv.isUnsigned()) {
        return std::to_string(typedToInt64(tv));
    }
    if (tv.isFloat()) {
        return std::to_string(typedToDouble(tv));
    }
    if (tv.isStrChar() || tv.isStrWide()) {
        return typedToStr(tv);
    }
    if (tv.isRational()) {
        return typedToStr(tv); // Rational::GetAsString() → "num\den"
    }
    // Вложенный Dict и открытые типы - в std::any-ветке.
    if (const std::any* a = std::get_if<std::any>(&tv.storage); a && a->has_value()) {
        if (a->type() == typeid(Dict)) {
            return dictToString(std::any_cast<const Dict&>(*a));
        }
        return anyToString(*a);
    }
    return "";
}
} // namespace detail

inline int64_t Dict::GetAsInteger() const {
    if (m_items.empty()) {
        throw std::runtime_error("trust::Dict: GetAsInteger on empty dict");
    }
    return detail::typedToInt64(m_items.front().second);
}

inline int64_t Dict::GetAsBoolean() const {
    if (m_items.empty()) {
        throw std::runtime_error("trust::Dict: GetAsBoolean on empty dict");
    }
    return detail::typedToInt64(m_items.front().second) != 0 ? 1 : 0;
}

inline double Dict::GetAsNumber() const {
    if (m_items.empty()) {
        throw std::runtime_error("trust::Dict: GetAsNumber on empty dict");
    }
    return detail::typedToDouble(m_items.front().second);
}

inline std::string Dict::GetAsString() const {
    if (m_items.empty()) {
        throw std::runtime_error("trust::Dict: GetAsString on empty dict");
    }
    return detail::typedToStr(m_items.front().second);
}

inline bool Dict::operator==(const Dict& other) const {
    if (m_items.size() != other.m_items.size()) {
        return false;
    }
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].first != other.m_items[i].first) {
            return false;
        }
        if (m_items[i].second.kind != other.m_items[i].second.kind) {
            return false;
        }
        if (!detail::typedEqual(m_items[i].second, other.m_items[i].second)) {
            return false;
        }
    }
    return true;
}

} // namespace trust

// Специализация std::formatter для trust::TypedValue: позволяет выводить элемент словаря
// напрямую через print/std::format без явного каста. Значение форматируется по TypeKind
// (группе) через trust::detail::typedValueToString. Декларируется за пределами namespace
// trust (специализация std-шаблона должна находиться в namespace std).
namespace std {
template <>
struct formatter<::trust::TypedValue> : formatter<string> {
    auto format(const ::trust::TypedValue& tv, format_context& ctx) const { return formatter<string>::format(::trust::detail::typedValueToString(tv), ctx); }
};

// Специализация std::formatter для trust::Dict: печать целого словаря через print/std::format
// в виде литерала `(имя=значение, ...)` (как dictToString). Позволяет выводить словарь целиком
// с именами элементов - в отличие от одиночного доступа `d.name` (только значение).
template <>
struct formatter<::trust::Dict> : formatter<string> {
    auto format(const ::trust::Dict& d, format_context& ctx) const { return formatter<string>::format(::trust::detail::dictToString(d), ctx); }
};
} // namespace std
