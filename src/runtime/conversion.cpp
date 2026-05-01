// #include "runtime/conversion.hpp"
// #include "types-old/type_template.hpp"
// #include <algorithm>

// namespace trust {

// // ============================================================================
// // format_as_string — specializations
// // ============================================================================

// template <>
// std::string format_as_string<Rational>(const Rational &v) {
//     return v.GetAsString();
// }

// template <>
// std::string format_as_string<Void>(const Void &) {
//     return "<void>";
// }

// template <>
// std::string format_as_string<UnsupportedType>(const UnsupportedType &) {
//     return "<unsupported>";
// }

// // ============================================================================
// // format_as_string for std::any
// // ============================================================================
// template <>
// std::string format_as_string<std::any>(const std::any &a) {
//     if (!a.has_value()) {
//         return "<any: empty>";
//     }
//     // Распаковываем и конвертируем в string
//     return detail::convert_any_to<std::string>(VarValue(a));
// }

// // ============================================================================
// // CategoryConverter — внутрикатегорийные конвертации + числовая иерархия
// //
// // Unified numeric converter:
// //   BoolCat → IntegerCat/FloatCat/ComplexCat
// //   IntegerCat → FloatCat/ComplexCat (via MaxInteger)
// //   FloatCat → ComplexCat (via MaxFloat)
// //   Same-cat conversions within numeric categories
// // ============================================================================

// // ---- IntegerCat: signed integer target, converts from BoolCat and IntegerCat ----
// template <TrustSignedInteger T>
// T detail::CategoryConverter<ConversionCategory::IntegerCat>::convert(const VarValue &value) {
//     return std::visit(
//         []<typename U>(const U &val) -> T {
//             constexpr auto src_cat = category_of_v<KindOf<U>::value>;

//             if constexpr (src_cat == ConversionCategory::IntegerCat) {
//                 // Внутрикатегорийная: через максимальный тип
//                 return static_cast<T>(static_cast<MaxInteger>(val));
//             } else if constexpr (src_cat == ConversionCategory::BoolCat) {
//                 // Bool → Integer: true=1, false=0
//                 return val ? T{1} : T{0};
//             } else if constexpr (src_cat == ConversionCategory::AnyCat) {
//                 return convert_any_to<T>(VarValue(val));
//             } else {
//                 FAULT_AS(ConversionError, "Var::as<{}>: cannot convert {} to integer", kind_to_string_impl(KindOf<T>::value),
//                          kind_to_string_impl(KindOf<U>::value));
//             }
//         },
//         value);
// }

// // Явные инстанциирования для всех типов категории IntegerCat
// template int detail::CategoryConverter<ConversionCategory::IntegerCat>::convert<int>(const VarValue &);
// template int64_t detail::CategoryConverter<ConversionCategory::IntegerCat>::convert<int64_t>(const VarValue &);

// // ---- FloatCat: floating-point target, converts from BoolCat, IntegerCat, FloatCat ----
// template <std::floating_point T>
// T detail::CategoryConverter<ConversionCategory::FloatCat>::convert(const VarValue &value) {
//     return std::visit(
//         []<typename U>(const U &val) -> T {
//             constexpr auto src_cat = category_of_v<KindOf<U>::value>;

//             if constexpr (src_cat == ConversionCategory::FloatCat) {
//                 return static_cast<T>(static_cast<MaxFloat>(val));
//             } else if constexpr (src_cat == ConversionCategory::IntegerCat) {
//                 // Integer → Float: через MaxIntermediate
//                 return static_cast<T>(static_cast<MaxInteger>(val));
//             } else if constexpr (src_cat == ConversionCategory::BoolCat) {
//                 return val ? T{1} : T{0};
//             } else if constexpr (src_cat == ConversionCategory::AnyCat) {
//                 return convert_any_to<T>(VarValue(val));
//             } else {
//                 FAULT_AS(ConversionError, "Var::as<{}>: cannot convert {} to float", kind_to_string_impl(KindOf<T>::value),
//                          kind_to_string_impl(KindOf<U>::value));
//             }
//         },
//         value);
// }

// // Явное инстанциирование для double
// template double detail::CategoryConverter<ConversionCategory::FloatCat>::convert<double>(const VarValue &);

// // ---- BoolCat: self-conversion only (no other categories convert TO bool) ----
// bool detail::CategoryConverter<ConversionCategory::BoolCat>::convert(const VarValue &value) {
//     return std::visit(
//         []<typename U>(const U &val) -> bool {
//             constexpr auto src_cat = category_of_v<KindOf<U>::value>;

//             if constexpr (src_cat == ConversionCategory::BoolCat) {
//                 return val;
//             } else if constexpr (src_cat == ConversionCategory::AnyCat) {
//                 return convert_any_to<bool>(VarValue(val));
//             } else {
//                 FAULT_AS(ConversionError, "Var::as<bool>: cannot convert {}", kind_to_string_impl(KindOf<U>::value));
//             }
//         },
//         value);
// }

// // ---- StringCat: string <-> wstring ----
// std::string detail::CategoryConverter<ConversionCategory::StringCat>::convert(const VarValue &value) {
//     return std::visit(
//         []<typename U>(const U &val) -> std::string {
//             constexpr auto src_cat = category_of_v<KindOf<U>::value>;

//             if constexpr (std::same_as<U, std::string>) {
//                 return val;
//             } else if constexpr (std::same_as<U, std::wstring>) {
//                 return narrow_string(val);
//             } else if constexpr (src_cat == ConversionCategory::AnyCat) {
//                 return convert_any_to<std::string>(VarValue(val));
//             } else {
//                 FAULT_AS(ConversionError, "Var::as<std::string>: cannot convert {} to string", kind_to_string_impl(KindOf<U>::value));
//             }
//         },
//         value);
// }

// // ---- RationalCat: converts from BoolCat, IntegerCat, RationalCat ----
// Rational detail::CategoryConverter<ConversionCategory::RationalCat>::convert(const VarValue &value) {
//     return std::visit(
//         []<typename U>(const U &val) -> Rational {
//             constexpr auto src_cat = category_of_v<KindOf<U>::value>;

//             if constexpr (std::same_as<U, Rational>) {
//                 // Self-conversion
//                 return val;
//             } else if constexpr (src_cat == ConversionCategory::IntegerCat) {
//                 // Integer → Rational: через MaxInteger
//                 return Rational(static_cast<MaxInteger>(val));
//             } else if constexpr (src_cat == ConversionCategory::BoolCat) {
//                 // Bool → Rational: true=1, false=0
//                 return Rational(val ? 1 : 0);
//             } else if constexpr (src_cat == ConversionCategory::AnyCat) {
//                 return convert_any_to<Rational>(VarValue(val));
//             } else {
//                 FAULT_AS(ConversionError, "Var::as<Rational>: cannot convert {}", kind_to_string_impl(KindOf<U>::value));
//             }
//         },
//         value);
// }

// // ---- TensorCat: self-conversion only ----
// Tensor detail::CategoryConverter<ConversionCategory::TensorCat>::convert(const VarValue &value) {
//     return std::visit(
//         []<typename U>(const U &val) -> Tensor {
//             if constexpr (std::same_as<U, Tensor>) {
//                 return val;
//             } else {
//                 FAULT_AS(ConversionError, "Var::as<Tensor>: cannot convert {}", kind_to_string_impl(KindOf<U>::value));
//             }
//         },
//         value);
// }

// // ---- AnyCat: wrap any value into std::any ----
// std::any detail::CategoryConverter<ConversionCategory::AnyCat>::convert(const VarValue &value) {
//     return std::visit(
//         [](const auto &val) -> std::any {
//             if constexpr (std::is_same_v<std::decay_t<decltype(val)>, Void>) {
//                 FAULT_AS(ConversionError, "Cannot convert Void to Any");
//             } else {
//                 return std::any(val);
//             }
//         },
//         value);
// }

// // ============================================================================
// // Var::to_string — delegates to format_as_string for each type
// // ============================================================================

// std::string Var::to_string() const {
//     switch (kind()) {
//     case VarKind::Void:
//         return format_as_string(strict_get<Void>());
//     case VarKind::Unsupported:
//         return format_as_string(strict_get<UnsupportedType>());
//     case VarKind::Any:
//         return format_as_string(strict_get<std::any>());
//     default:
//         // Для типов, которые могут быть конвертированы в string внутри своей категории
//         if (is<std::string>()) {
//             return strict_get<std::string>();
//         } else if (is<std::wstring>()) {
//             return narrow_string(strict_get<std::wstring>());
//         } else if (is<int>()) {
//             return format_as_string(strict_get<int>());
//         } else if (is<double>()) {
//             return format_as_string(strict_get<double>());
//         } else if (is<int64_t>()) {
//             return format_as_string(strict_get<int64_t>());
//         } else if (is<bool>()) {
//             return strict_get<bool>() ? "true" : "false";
//         } else if (is<Rational>()) {
//             return format_as_string(strict_get<Rational>());
//         } else if (is<std::vector<int64_t>>()) {
//             return format_vector_as_string(strict_get<std::vector<int64_t>>());
//         } else if (is<std::vector<double>>()) {
//             return format_vector_as_string(strict_get<std::vector<double>>());
//         } else if (is<Tensor>()) {
//             if (!strict_get<Tensor>())
//                 return "<tensor: empty>";
//             return "<tensor>";
//         } else {
//             FAULT("Var::to_string: unhandled VarKind");
//         }
//     }
// }

// // ============================================================================
// // Var::operator=
// // ============================================================================

// Var &Var::operator=(Var other) noexcept {
//     swap(*this, other);
//     return *this;
// }

// // ============================================================================
// // kind_to_string_impl
// // ============================================================================

// std::string kind_to_string_impl(VarKind k) {
//     switch (k) {
// #define __V_TSTR(k, ...) \
//     case VarKind::k:     \
//         return #k;
//         TRUST_VAR_TYPES_ALL(__V_TSTR)
// #undef __V_TSTR
//     }
//     FAULT("kind_to_string_impl: unknown VarKind value {}", static_cast<size_t>(k));
// }

// // ============================================================================
// // widen / narrow
// // ============================================================================

// std::wstring widen_string(const std::string &s) {
//     std::wstring result;
//     result.reserve(s.size());
//     for (unsigned char c : s) {
//         result += static_cast<wchar_t>(c);
//     }
//     return result;
// }

// std::string narrow_string(const std::wstring &s) {
//     std::string result;
//     result.reserve(s.size());
//     for (wchar_t wc : s) {
//         result += static_cast<char>(wc & 0xFF);
//     }
//     return result;
// }

// } // namespace trust