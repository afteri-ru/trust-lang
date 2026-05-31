#include "types/category.hpp"
#include "types/numbers.hpp"
#include "types/types.hpp"

namespace trust {

namespace detail {

Any convert_integer(const Integers& src, TypeKind target) {
    auto cat = KindOps::category_of(target);
    if (cat == Category::Integers) {
        if (src.would_overflow_to(target))
            throw std::overflow_error("overflow");
        return Integers{src.get(), target};
    }
    if (cat == Category::Numbers)
        return Float{static_cast<double>(src.get()), target};
    // if (cat == Category::Complex)
    //     return Complex{std::complex<double>{static_cast<double>(src.get()), 0.0}, target};
    throw std::invalid_argument("cannot convert");
}

Any convert_float(const Float& src, TypeKind target) {
    auto cat = KindOps::category_of(target);
    if (cat == Category::Numbers) {
        if (src.would_overflow_to(target))
            throw std::overflow_error("overflow");
        return Float{src.get(), target};
    }
    if (cat == Category::Integers) {
        if (src.would_overflow_to_int(target))
            throw std::overflow_error("overflow");
        return Integers{static_cast<int64_t>(src.get()), target};
    }
    // if (cat == Category::Complex)
    //     return Complex{std::complex<double>{src.get(), 0.0}, target};
    throw std::invalid_argument("cannot convert");
}

// Any convert_complex(const Complex &src, TypeKind target) {
//     auto cat = KindOps::category_of(target);
//     if (cat == Category::Complex)
//         return Complex{src.get(), target};
//     if (cat == Category::Numbers) {
//         if (src.has_imaginary())
//             throw std::invalid_argument("cannot convert imaginary to float");
//         return Float{src.get().real(), target};
//     }
//     if (cat == Category::Integer) {
//         if (src.has_imaginary())
//             throw std::invalid_argument("cannot convert imaginary to int");
//         return Integer{static_cast<int64_t>(src.get().real()), target};
//     }
//     throw std::invalid_argument("cannot convert");
// }

} // namespace detail

Any runtime_convert(const Any& val, TypeKind target) {
    if (target == TypeKind::Void)
        return Void{};
    return std::visit(
        [&](auto&& v) -> Any {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Void>)
                return Void{};
            else if constexpr (std::is_same_v<T, Integers>)
                return detail::convert_integer(v, target);
            else if constexpr (std::is_same_v<T, Float>)
                return detail::convert_float(v, target);
            // else if constexpr (std::is_same_v<T, Complex>)
            //     return detail::convert_complex(v, target);
            else
                throw std::invalid_argument("not implemented");
        },
        val);
}

std::string stringify_value(const Any& val, bool wi) {
    return std::visit([&](auto&& v) -> std::string { return v.to_string(wi); }, val);
}

} // namespace trust