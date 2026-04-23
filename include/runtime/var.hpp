#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "runtime/rational.hpp"
#include "runtime/tensor.hpp"
#include "runtime/var_conversions.hpp"
#include "runtime/var_template.hpp"
#include "types/type_info.hpp"
#include "diag/error.hpp"

namespace trust {

// ============================================================================
// Stub types
// ============================================================================
struct Void {};            // Void value (replaces std::monostate)
struct UnsupportedType {}; // Placeholder for removed types

// Must be defined before X-macros so KindOf<T> can reference it
// Tensor is a type-erased handle — see TensorHandle in runtime/tensor.hpp
// ============================================================================
using Tensor = TensorHandle;

// ============================================================================
// X-macro — SINGLE SOURCE OF TRUTH for all Var types
// Format: V(Kind, CppType, ConversionCategory, MinOutputFormat)
// VarKind value = std::variant index. New types MUST be added at the END.
//
// IMPORTANT: Whitespace-separated, NO commas. Consumers add their own:
//   std::variant<T1, T2>  -> comma after each via __VT_COMMA(k,t) = t,
//   enum { K1, K2 }       -> comma after each via __VK(k,t)   = k,
//   template<> struct X{};-> NO comma (semicolons only)
// ============================================================================
#define TRUST_VAR_TYPES(V)                       \
    V(Void, Void, VoidCat, Trad)                 \
    V(Int, int, NumericCat, Trad)                \
    V(Bool, bool, BoolCat, Trad)                 \
    V(Double, double, NumericCat, Trad)          \
    V(Int64, int64_t, NumericCat, Trad)          \
    V(String, std::string, StringCat, Trad)      \
    V(WideString, std::wstring, StringCat, Trad) \
    V(Rational, Rational, RationalCat, Trad)     \
    /* DONT REMOVE SUPPORTED_VECTORS_EXPAND */   \
    SUPPORTED_VECTORS_EXPAND(V)                  \
    V(Tensor, Tensor, TensorCat, Trad)

// **DONT REMOVE TRUST_VAR_TYPES_LAST**!
// Last element for X-macro — TRUST_VAR_TYPES_ALL
// Make last element of list without trayler comma
// ============================================================================
#define TRUST_VAR_TYPES_LAST(V) V(Unsupported, UnsupportedType, VoidCat, Trad)

#define TRUST_VAR_TYPES_ALL(V) TRUST_VAR_TYPES(V) TRUST_VAR_TYPES_LAST(V)

// ============================================================================
// ConversionCategory — категория типа для автоматической генерации конверсий
// ============================================================================
enum class ConversionCategory { VoidCat, NumericCat, BoolCat, StringCat, RationalCat, VectorCat, TensorCat };

// ============================================================================
// VarKind — values = variant indices
// ============================================================================
enum class VarKind : size_t {
#define __VK(k, t, cat, fmt) k,
    TRUST_VAR_TYPES(__VK) TRUST_VAR_TYPES_LAST(__VK)
#undef __VK
};

[[nodiscard]] std::string kind_to_string_impl(VarKind k);

// ============================================================================
// CategoryOf<K> — mapping: VarKind -> ConversionCategory
// ============================================================================
template <VarKind K>
struct CategoryOf;

#define __VK_CAT(k, t, cat, fmt)                                             \
    template <>                                                              \
    struct CategoryOf<VarKind::k> {                                          \
        static constexpr ConversionCategory value = ConversionCategory::cat; \
    };
TRUST_VAR_TYPES_ALL(__VK_CAT)
#undef __VK_CAT

template <VarKind K>
inline constexpr ConversionCategory category_of_v = CategoryOf<K>::value;

// ============================================================================
// CategoryPredicates — compile-time helpers
// ============================================================================
template <VarKind K>
struct IsVoidKind : std::bool_constant<category_of_v<K> == ConversionCategory::VoidCat> {};
template <VarKind K>
struct IsNumericKind : std::bool_constant<category_of_v<K> == ConversionCategory::NumericCat> {};
template <VarKind K>
struct IsBoolKind : std::bool_constant<category_of_v<K> == ConversionCategory::BoolCat> {};
template <VarKind K>
struct IsStringKind : std::bool_constant<category_of_v<K> == ConversionCategory::StringCat> {};
template <VarKind K>
struct IsRationalKind : std::bool_constant<category_of_v<K> == ConversionCategory::RationalCat> {};
template <VarKind K>
struct IsVectorKind : std::bool_constant<category_of_v<K> == ConversionCategory::VectorCat> {};
template <VarKind K>
struct IsTensorKind : std::bool_constant<category_of_v<K> == ConversionCategory::TensorCat> {};

template <VarKind K>
inline constexpr bool is_void_v = IsVoidKind<K>::value;
template <VarKind K>
inline constexpr bool is_numeric_v = IsNumericKind<K>::value;
template <VarKind K>
inline constexpr bool is_bool_v = IsBoolKind<K>::value;
template <VarKind K>
inline constexpr bool is_string_v = IsStringKind<K>::value;
template <VarKind K>
inline constexpr bool is_rational_v = IsRationalKind<K>::value;
template <VarKind K>
inline constexpr bool is_vector_v = IsVectorKind<K>::value;
template <VarKind K>
inline constexpr bool is_tensor_v = IsTensorKind<K>::value;

// ============================================================================
// Автоматическая генерация IsConversionEnabled из категорий
// ============================================================================
namespace detail {

template <ConversionCategory From, ConversionCategory To>
consteval bool is_category_convertible() {
    if constexpr (From == ConversionCategory::VoidCat)
        return false;
    if constexpr (To == ConversionCategory::VoidCat)
        return false;

    if constexpr (From == ConversionCategory::NumericCat) {
        return To == ConversionCategory::NumericCat || To == ConversionCategory::BoolCat || To == ConversionCategory::StringCat ||
               To == ConversionCategory::RationalCat;
    }

    if constexpr (From == ConversionCategory::BoolCat) {
        return To == ConversionCategory::NumericCat || To == ConversionCategory::StringCat;
    }

    if constexpr (From == ConversionCategory::StringCat) {
        return To == ConversionCategory::NumericCat || To == ConversionCategory::BoolCat;
    }

    if constexpr (From == ConversionCategory::RationalCat) {
        return To == ConversionCategory::NumericCat || To == ConversionCategory::BoolCat || To == ConversionCategory::StringCat;
    }

    if constexpr (From == ConversionCategory::VectorCat) {
        return To == ConversionCategory::StringCat;
    }

    if constexpr (From == ConversionCategory::TensorCat) {
        return To == ConversionCategory::StringCat;
    }

    return false;
}

template <VarKind From, VarKind To>
consteval bool is_conversion_enabled_impl() {
    if constexpr (From == To)
        return true;
    return is_category_convertible<category_of_v<From>, category_of_v<To>>();
}

} // namespace detail

// ============================================================================
// VarKindCount — derived from number of entries
// ============================================================================
#define __VC(k, t, cat, fmt) +1
inline constexpr size_t VarKindCount = 0 TRUST_VAR_TYPES(__VC) TRUST_VAR_TYPES_LAST(__VC);
#undef __VC

// ============================================================================
// TypeOfKind<VarKind::X> — reverse mapping: VarKind -> C++ type
// ============================================================================
template <VarKind K>
struct TypeOfKind;

#define __VTOT(k, t, cat, fmt)      \
    template <>                     \
    struct TypeOfKind<VarKind::k> { \
        using impl_type = t;        \
    };
TRUST_VAR_TYPES_ALL(__VTOT)
#undef __VTOT

template <VarKind K>
using TypeOfKind_t = typename TypeOfKind<K>::impl_type;

// ============================================================================
// KindOf<T> — compile-time VarKind for a C++ type
// ============================================================================
template <typename T>
struct KindOf {
    static_assert(sizeof(T) == 0, "KindOf<T>: T is not a supported Var type");
};

#define __VKOF(k, t, cat, fmt)                       \
    template <>                                      \
    struct KindOf<t> {                               \
        static constexpr VarKind value = VarKind::k; \
    };
TRUST_VAR_TYPES_ALL(__VKOF)
#undef __VKOF

template <typename T>
inline constexpr VarKind kind_of_v = KindOf<T>::value;

// ============================================================================
// IsConversionEnabled<FromKind, ToKind>
// ============================================================================
template <VarKind From, VarKind To>
struct IsConversionEnabled : std::bool_constant<detail::is_conversion_enabled_impl<From, To>()> {};

template <VarKind From, VarKind To>
inline constexpr bool IsConversionEnabled_v = IsConversionEnabled<From, To>::value;

// ============================================================================
// HasAnyConversion<ToKind>
// ============================================================================
template <VarKind To, size_t... Is>
consteval bool has_any_conversion_impl(std::index_sequence<Is...>) {
    return (IsConversionEnabled_v<static_cast<VarKind>(Is), To> || ...);
}

template <VarKind To>
struct HasAnyConversion : std::bool_constant<has_any_conversion_impl<To>(std::make_index_sequence<VarKindCount>{})> {};

// ============================================================================
// HasOnlySelfIncoming<Kind> — true if ONLY identity can convert TO Kind
// ============================================================================
template <VarKind K, size_t... Is>
consteval bool has_only_self_incoming_impl(std::index_sequence<Is...>) {
    return ((IsConversionEnabled_v<static_cast<VarKind>(Is), K> == (static_cast<VarKind>(Is) == K)) && ...);
}

template <VarKind K>
inline constexpr bool HasOnlySelfIncoming_v = has_only_self_incoming_impl<K>(std::make_index_sequence<VarKindCount>{});

// ============================================================================
// VarValue — std::variant automatically generated from TRUST_VAR_TYPES
// ============================================================================
#define __VV(k, t, cat, fmt) t,
#define __VVL(k, t, cat, fmt) t
using VarValue = std::variant<TRUST_VAR_TYPES(__VV) TRUST_VAR_TYPES_LAST(__VVL)>;
#undef __VV
#undef __VVL

// ============================================================================
// Supported type trait
// ============================================================================
template <typename T>
struct IsSupportedVarType : std::false_type {};

#define __V_IST(k, t, cat, fmt) \
    template <>                 \
    struct IsSupportedVarType<t> : std::true_type {};
TRUST_VAR_TYPES_ALL(__V_IST)
#undef __V_IST

template <typename T>
concept VarSupportedType = IsSupportedVarType<T>::value;

// ============================================================================
// Var
// ============================================================================
class Var {
  public:
    using Value = VarValue;

    Var() = default;

    // Generated constructors
#define __V_CTOR(k, t, cat, fmt) \
    explicit Var(t v) : m_value(std::move(v)) {}
    TRUST_VAR_TYPES_ALL(__V_CTOR)
#undef __V_CTOR

    Var(Var const &) = default;
    Var(Var &&) = default;
    Var &operator=(Var other) noexcept;

    [[nodiscard]] VarKind kind() const { return static_cast<VarKind>(m_value.index()); }
    [[nodiscard]] std::string to_string() const;

    template <VarSupportedType T>
    [[nodiscard]] bool is() const {
        return std::holds_alternative<T>(m_value);
    }

    template <VarSupportedType T>
    [[nodiscard]] T const &strict_get() const {
        auto const *p = std::get_if<T>(&m_value);
        if (!p) {
            FAULT_AS(ConversionError, "Var::strict_get<{}>: actual kind is {}", kind_to_string_impl(kind_of_v<T>), kind_to_string_impl(kind()));
        }
        return *p;
    }

    template <VarSupportedType T>
    [[nodiscard]] std::optional<T const *> try_get() const noexcept {
        auto p = std::get_if<T>(&m_value);
        if (!p)
            return std::nullopt;
        return p;
    }

    template <VarSupportedType T>
    [[nodiscard]] T as() const;

  private:
    template <VarSupportedType T>
    [[nodiscard]] T as_trivial() const {
        return strict_get<T>();
    }

    template <VarSupportedType T>
    [[nodiscard]] T as_nontrivial() const;

    friend void swap(Var &a, Var &b) noexcept {
        using std::swap;
        swap(a.m_value, b.m_value);
    }

  private:
    Value m_value;
};

// ============================================================================
// Compile-time checks
// ============================================================================
static_assert(std::variant_size_v<Var::Value> == VarKindCount, "VarKind/variant size mismatch: add new types at the END of TRUST_VAR_TYPES");

#define __V_CHK(k, t, cat, fmt) static_assert(IsConversionEnabled_v<VarKind::k, VarKind::k>, "Conversion matrix missing self-conversion for " #t);
TRUST_VAR_TYPES_ALL(__V_CHK)
#undef __V_CHK

// ============================================================================
// CategoryConverter — стратегия конверсий на уровне категорий
// ============================================================================
namespace detail {

template <ConversionCategory Cat>
struct CategoryConverter;

// ---- NumericCat ----
template <>
struct CategoryConverter<ConversionCategory::NumericCat> {
    template <typename T>
        requires(std::integral<T> || std::floating_point<T>)
    static T convert(const VarValue &value);
};

// ---- BoolCat ----
template <>
struct CategoryConverter<ConversionCategory::BoolCat> {
    static bool convert(const VarValue &value);
};

// ---- StringCat ----
template <>
struct CategoryConverter<ConversionCategory::StringCat> {
    static std::string convert(const VarValue &value);
};

// ---- RationalCat ----
template <>
struct CategoryConverter<ConversionCategory::RationalCat> {
    static Rational convert(const VarValue &value);
};

// ConversionTarget — делегирует в CategoryConverter по категории типа
// Порядок важен: bool проверяется до integral (т.к. bool — integral в C++)
template <typename T>
struct ConversionTarget {
    static T convert(const VarValue &value) {
        constexpr auto cat = category_of_v<kind_of_v<T>>;
        if constexpr (std::same_as<T, bool>) {
            return CategoryConverter<ConversionCategory::BoolCat>::convert(value);
        } else if constexpr (std::same_as<T, std::string>) {
            return CategoryConverter<ConversionCategory::StringCat>::convert(value);
        } else if constexpr (std::same_as<T, Rational>) {
            return CategoryConverter<ConversionCategory::RationalCat>::convert(value);
        } else if constexpr (std::integral<T> || std::floating_point<T>) {
            return CategoryConverter<ConversionCategory::NumericCat>::template convert<T>(value);
        }
    }
};

template <>
struct ConversionTarget<std::wstring> {
    static std::wstring convert(const VarValue &value);
};

} // namespace detail

// ============================================================================
// as<T>() — primary template с ветвлением через if constexpr
// ============================================================================
template <VarSupportedType T>
[[nodiscard]] inline T Var::as() const {
    static_assert(HasAnyConversion<kind_of_v<T>>::value, "as<T>: no conversion path exists from ANY source type to target. "
                                                         "You must add entries to TRUST_VAR_CONVERSIONS.");
    if constexpr (HasOnlySelfIncoming_v<kind_of_v<T>>) {
        return strict_get<T>();
    } else {
        return detail::ConversionTarget<T>::convert(m_value);
    }
}

} // namespace trust