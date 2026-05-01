#include "types/forward.hpp"
#include "types/numbers.hpp"
#include "types/integers.hpp"
#include "stdlib/buildin.hpp"
#include "types/types.hpp"

#include <limits>
#include <climits>

namespace trust {

namespace {

bool would_overflow_to_float_impl(double value, TypeKind target) {
    switch (target) {
    case TypeKind::Float64:
        return false;
    case TypeKind::Float32: {
        auto f = static_cast<float>(value);
        return f == std::numeric_limits<float>::infinity() || f == -std::numeric_limits<float>::infinity() || (value != 0.0 && f == 0.0);
    }
    case TypeKind::Float16:
        return value > 65504.0 || value < -65504.0;
    default:
        return true;
    }
}

bool would_overflow_from_float_impl(double value, TypeKind target) {
    switch (target) {
    case TypeKind::Int64:
        return value < static_cast<double>(INT64_MIN) || value > static_cast<double>(INT64_MAX);
    case TypeKind::Int32:
        return value < static_cast<double>(INT32_MIN) || value > static_cast<double>(INT32_MAX);
    case TypeKind::Int16:
        return value < static_cast<double>(INT16_MIN) || value > static_cast<double>(INT16_MAX);
    case TypeKind::Int8:
        return value < static_cast<double>(INT8_MIN) || value > static_cast<double>(INT8_MAX);
    case TypeKind::Bool:
        return value != 0.0 && value != 1.0;
    default:
        return true;
    }
}

} // namespace

bool Float::would_overflow_to(TypeKind t) const {
    return would_overflow_to_float_impl(value_, t);
}

bool Float::would_overflow_to_int(TypeKind t) const {
    return would_overflow_from_float_impl(value_, t);
}

bool would_overflow_to_float(double value, TypeKind target) {
    return would_overflow_to_float_impl(value, target);
}

bool would_overflow_from_float(double value, TypeKind target) {
    return would_overflow_from_float_impl(value, target);
}

Value &Float::convert_to(TypeKind t, Value &dest) const {
    auto cat = KindOps::category_of(t);
    if (cat == Category::Numbers) {
        if (would_overflow_to(t))
            throw std::overflow_error("overflow");
        if (dest.kind() == t)
            static_cast<Float &>(dest) = Float{value_, t};
        return dest;
    }
    if (cat == Category::Integers) {
        if (would_overflow_to_int(t))
            throw std::overflow_error("overflow");
        if (dest.kind() == t)
            static_cast<Integers &>(dest) = Integers{static_cast<int64_t>(value_), t};
        return dest;
    }
    // if (cat == Category::Complex) {
    //     if (dest.kind() == t)
    //         static_cast<Complex &>(dest) = Complex{std::complex<double>{value_, 0.0}, t};
    //     return dest;
    // }
    throw std::invalid_argument("cannot convert");
}

void Float::_register(Types &t) {
    t.add(TypeInfo(TypeKind::Float64, "double"));
    t.add(TypeInfo(TypeKind::Float32, "float"));

    t.add(TypeInfo(TypeKind::Single, "float"));
    t.add(TypeInfo(TypeKind::Double, "double"));
    t.add(TypeInfo(TypeKind::Numbers, "double"));

    t.add(TypeInfo(TypeKind::Float16, "std::bfloat16_t", LanguageVersion::CPP23));
    t.add_headers({TypeKind::Float16}, {"<stdfloat>"});
}

} // namespace trust