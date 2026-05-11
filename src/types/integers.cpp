#include "types/integers.hpp"
#include "types/numbers.hpp"
#include "types/types.hpp"

#include <climits>
#include <array>

namespace trust {

// Limits table indexed by size_index: [min, max] for each signed integer width
static constexpr std::array<std::pair<int64_t, int64_t>, 4> kIntegerLimits = {{
    {INT8_MIN, INT8_MAX},   // index 0: Int8
    {INT16_MIN, INT16_MAX}, // index 1: Int16
    {INT32_MIN, INT32_MAX}, // index 2: Int32
    {INT64_MIN, INT64_MAX}, // index 3: Int64
}};

bool Integers::would_overflow_to(TypeKind t) const {
    auto idx = KindOps::index_of(t);
    auto grp = KindOps::group_of(t);

    // Bool: only 0 and 1 are valid
    if (grp == static_cast<uint8_t>(SubInteger::Bool)) {
        return value_ != 0 && value_ != 1;
    }

    // Out of table range — cannot represent
    if (idx >= kIntegerLimits.size()) {
        return true;
    }

    const auto& [lo, hi] = kIntegerLimits[idx];
    return value_ < lo || value_ > hi;
}

Value& Integers::convert_to(TypeKind t, Value& dest) const {
    auto cat = KindOps::category_of(t);
    if (cat == Category::Integers) {
        if (would_overflow_to(t))
            throw std::overflow_error("overflow");
        if (dest.kind() == t)
            static_cast<Integers&>(dest) = Integers{value_, t};
        return dest;
    }
    if (cat == Category::Numbers) {
        if (dest.kind() == t)
            static_cast<Float&>(dest) = Float{static_cast<double>(value_), t};
        return dest;
    }
    // if (cat == Category::Complex) {
    //     if (dest.kind() == t)
    //         static_cast<Complex &>(dest) = Complex{std::complex<double>{static_cast<double>(value_), 0.0}, t};
    //     return dest;
    // }
    throw std::invalid_argument("cannot convert");
}

void Integers::_register(Types& t) {
    // Common type for all integer values
    t.add(TypeInfo(TypeKind::Integers, "int64_t"));

    t.add(TypeInfo(TypeKind::Int64, "int64_t"));
    t.add(TypeInfo(TypeKind::Int32, "int32_t"));
    t.add(TypeInfo(TypeKind::Int16, "int16_t"));
    t.add(TypeInfo(TypeKind::Int8, "int8_t"));
    t.add(TypeInfo(TypeKind::Bool, "bool"));

    // Type aliases for mangling C++ names
    t.add(TypeInfo(TypeKind::Char, "char"));
    t.add(TypeInfo(TypeKind::Byte, "uint8_t"));
    t.add(TypeInfo(TypeKind::Word, "uint16_t"));
    t.add(TypeInfo(TypeKind::DWord, "uint32_t"));
    t.add(TypeInfo(TypeKind::DDWord, "uint64_t"));
}

} // namespace trust