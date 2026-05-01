// #pragma once

// #include "types-old/types.hpp"
// #include "types-old/type_template.hpp"
// #include "runtime/rational.hpp"
// #include "runtime/tensor.hpp"

// namespace trust {

// // ============================================================================
// // VarValue — std::variant containing all Var-supported types
// // Defined here (not in types.hpp) to avoid pulling in Rational/Tensor headers
// // ============================================================================
// #define __VV(k, t, cat, fmt) t,
// #define __VVL(k, t, cat, fmt) t
// using VarValue = std::variant<TRUST_VAR_TYPES(__VV) TRUST_VAR_TYPES_LAST(__VVL)>;
// #undef __VV
// #undef __VVL

// // ============================================================================
// // TRUST_VAR_VECTOR_CONVERSIONS — generated macro for vector conversions
// // Must be defined AFTER VarKind is declared (now in types.hpp)
// // ============================================================================
// #define TRUST_VAR_VECTOR_CONVERSIONS(M) SUPPORTED_VECTORS_EXPAND(M)

// // ============================================================================
// // Var
// // ============================================================================
// class Var {
//   public:
//     using Value = VarValue;

//     Var() = default;

//     // Generated constructors
// #define __V_CTOR(k, t, cat, fmt) \
//     explicit Var(t v) : m_value(std::move(v)) {}
//     TRUST_VAR_TYPES_ALL(__V_CTOR)
// #undef __V_CTOR

//     Var(Var const &) = default;
//     Var(Var &&) = default;
//     Var &operator=(Var other) noexcept;

//     [[nodiscard]] VarKind kind() const { return static_cast<VarKind>(m_value.index()); }
//     [[nodiscard]] std::string to_string() const;

//     template <VarSupportedType T>
//     [[nodiscard]] bool is() const {
//         return std::holds_alternative<T>(m_value);
//     }

//     template <VarSupportedType T>
//     [[nodiscard]] T const &strict_get() const {
//         auto const *p = std::get_if<T>(&m_value);
//         if (!p) {
//             FAULT_AS(ConversionError, "Var::strict_get<{}>: actual kind is {}", kind_to_string_impl(kind_of_v<T>), kind_to_string_impl(kind()));
//         }
//         return *p;
//     }

//     template <VarSupportedType T>
//     [[nodiscard]] std::optional<T const *> try_get() const noexcept {
//         auto p = std::get_if<T>(&m_value);
//         if (!p)
//             return std::nullopt;
//         return p;
//     }

//     template <VarSupportedType T>
//     [[nodiscard]] T as() const;

//   private:
//     template <VarSupportedType T>
//     [[nodiscard]] T as_trivial() const {
//         return strict_get<T>();
//     }

//     template <VarSupportedType T>
//     [[nodiscard]] T as_nontrivial() const;

//     friend void swap(Var &a, Var &b) noexcept {
//         using std::swap;
//         swap(a.m_value, b.m_value);
//     }

//   private:
//     Value m_value;
// };

// // ============================================================================
// // Compile-time checks
// // ============================================================================
// static_assert(std::variant_size_v<Var::Value> == VarKindCount, "VarKind/variant size mismatch: add new types at the END of TRUST_VAR_TYPES");

// #define __V_CHK(k, t, cat, fmt) static_assert(IsConversionEnabled_v<VarKind::k, VarKind::k>, "Conversion matrix missing self-conversion for " #t);
// TRUST_VAR_TYPES_ALL(__V_CHK)
// #undef __V_CHK

// } // namespace trust
