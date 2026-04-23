#include "runtime/var.hpp"
#include "runtime/var_template.hpp" // format_vector_as_string
#include "runtime/tensor.hpp"

namespace trust {

// ============================================================================
// format_as_string — specializations
// ============================================================================

template <>
std::string format_as_string<Rational>(const Rational &v) {
    return v.GetAsString();
}

template <>
std::string format_as_string<Void>(const Void &) {
    return "<void>";
}

template <>
std::string format_as_string<UnsupportedType>(const UnsupportedType &) {
    return "<unsupported>";
}

std::string format_as_tensor_as_string(const TensorHandle &h) {
    if (!h) {
        return "<tensor: empty>";
    }
    return "<tensor>";
}

// ============================================================================
// CategoryConverter — стратегия конверсий на уровне категорий
// ============================================================================

// ---- NumericCat ----
template <typename T>
    requires(std::integral<T> || std::floating_point<T>)
T detail::CategoryConverter<ConversionCategory::NumericCat>::convert(const VarValue &value) {
    return std::visit(
        []<typename U>(const U &val) -> T {
            constexpr auto src_kind = KindOf<U>::value;
            constexpr auto src_cat = category_of_v<src_kind>;

            if constexpr (src_cat == ConversionCategory::VoidCat || src_cat == ConversionCategory::VectorCat || src_cat == ConversionCategory::TensorCat) {
                FAULT_AS(ConversionError, "Var::as<{}>: cannot convert {} to numeric target", kind_to_string_impl(KindOf<T>::value),
                         kind_to_string_impl(src_kind));
            } else if constexpr (src_cat == ConversionCategory::BoolCat) {
                return numeric::cross_convert_from_bool<T>(val);
            } else if constexpr (src_cat == ConversionCategory::RationalCat) {
                return static_cast<T>(val.GetAsNumber());
            } else if constexpr (src_cat == ConversionCategory::StringCat) {
                if constexpr (std::same_as<U, std::string>) {
                    return parse_numeric<T>(std::string_view(val));
                } else {
                    return parse_numeric<T>(narrow_string(val));
                }
            } else {
                // NumericCat -> NumericCat
                return numeric::cross_convert<T>(val);
            }
        },
        value);
}

// Явное инстанциирование для всех числовых типов
template int detail::CategoryConverter<ConversionCategory::NumericCat>::convert<int>(const VarValue &);
template double detail::CategoryConverter<ConversionCategory::NumericCat>::convert<double>(const VarValue &);
template int64_t detail::CategoryConverter<ConversionCategory::NumericCat>::convert<int64_t>(const VarValue &);

// ---- BoolCat ----
bool detail::CategoryConverter<ConversionCategory::BoolCat>::convert(const VarValue &value) {
    return std::visit(
        []<typename U>(const U &val) -> bool {
            constexpr auto src_cat = category_of_v<KindOf<U>::value>;

            if constexpr (std::same_as<U, bool>) {
                return val;
            } else if constexpr (src_cat == ConversionCategory::NumericCat) {
                return numeric::cross_convert_to_bool(val);
            } else if constexpr (src_cat == ConversionCategory::RationalCat) {
                return val.GetAsBoolean() != 0;
            } else if constexpr (src_cat == ConversionCategory::StringCat) {
                if constexpr (std::same_as<U, std::string>) {
                    return parse_as_bool(std::string_view(val));
                } else {
                    if (val == L"true")
                        return true;
                    if (val == L"false")
                        return false;
                    FAULT_AS(ParseError, "Cannot convert wstring '{}' to bool", narrow_string(val));
                }
            } else {
                FAULT_AS(ConversionError, "Var::as<bool>: cannot convert {} to bool", kind_to_string_impl(KindOf<U>::value));
            }
        },
        value);
}

// ---- StringCat ----
std::string detail::CategoryConverter<ConversionCategory::StringCat>::convert(const VarValue &value) {
    return std::visit(
        []<typename U>(const U &val) -> std::string {
            constexpr auto src_cat = category_of_v<KindOf<U>::value>;

            if constexpr (std::same_as<U, std::string>) {
                return val;
            } else if constexpr (std::same_as<U, std::wstring>) {
                return narrow_string(val);
            } else if constexpr (src_cat == ConversionCategory::NumericCat || src_cat == ConversionCategory::BoolCat ||
                                 src_cat == ConversionCategory::RationalCat) {
                return format_as_string(val);
            } else if constexpr (src_cat == ConversionCategory::VectorCat) {
                return format_vector_as_string(val);
            } else if constexpr (src_cat == ConversionCategory::TensorCat) {
                return format_as_tensor_as_string(val);
            } else {
                FAULT_AS(ConversionError, "Var::as<std::string>: cannot convert {} to string", kind_to_string_impl(KindOf<U>::value));
            }
        },
        value);
}

// ---- RationalCat ----
Rational detail::CategoryConverter<ConversionCategory::RationalCat>::convert(const VarValue &value) {
    return std::visit(
        []<typename U>(const U &val) -> Rational {
            constexpr auto src_cat = category_of_v<KindOf<U>::value>;

            if constexpr (std::same_as<U, Rational>) {
                return val;
            } else if constexpr (src_cat == ConversionCategory::NumericCat) {
                return Rational(val);
            } else if constexpr (src_cat == ConversionCategory::BoolCat) {
                return Rational(val ? 1 : 0);
            } else {
                FAULT_AS(ConversionError, "Var::as<Rational>: cannot convert {} to Rational", kind_to_string_impl(KindOf<U>::value));
            }
        },
        value);
}

// ---- WideStringCat — делегирует через StringCat, затем widen ----
std::wstring detail::ConversionTarget<std::wstring>::convert(const VarValue &value) {
    std::string narrow = detail::CategoryConverter<ConversionCategory::StringCat>::convert(value);
    return widen_string(narrow);
}

// ============================================================================
// Var::to_string — делегирует в as<std::string>() для конвертируемых типов,
// Void и Unsupported обрабатываются напрямую через format_as_string
// ============================================================================

std::string Var::to_string() const {
    switch (kind()) {
    case VarKind::Void:
        return format_as_string(strict_get<Void>());
    case VarKind::Unsupported:
        return format_as_string(strict_get<UnsupportedType>());
    default:
        return as<std::string>();
    }
}

// ============================================================================
// Var::operator=
// ============================================================================

Var &Var::operator=(Var other) noexcept {
    swap(*this, other);
    return *this;
}

// ============================================================================
// kind_to_string_impl
// ============================================================================

std::string kind_to_string_impl(VarKind k) {
    switch (k) {
#define __V_TSTR(k, ...) \
    case VarKind::k:     \
        return #k;
        TRUST_VAR_TYPES_ALL(__V_TSTR)
#undef __V_TSTR
    }
    FAULT("kind_to_string_impl: unknown VarKind value {}", static_cast<size_t>(k));
}

// ============================================================================
// parse_as_bool
// ============================================================================

bool parse_as_bool(std::string_view s) {
    if (s == "true")
        return true;
    if (s == "false")
        return false;
    FAULT_AS(ParseError, "Cannot convert string '{}' to bool", s);
}

// ============================================================================
// widen / narrow
// ============================================================================

std::wstring widen_string(const std::string &s) {
    std::wstring result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        result += static_cast<wchar_t>(c);
    }
    return result;
}

std::string narrow_string(const std::wstring &s) {
    std::string result;
    result.reserve(s.size());
    for (wchar_t wc : s) {
        result += static_cast<char>(wc & 0xFF);
    }
    return result;
}

} // namespace trust