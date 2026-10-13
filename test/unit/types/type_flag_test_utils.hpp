#pragma once

// test/unit/types/type_flag_test_utils.hpp
// Test-only bit helpers for preparing state-bearing TypeId values in unit tests.
// Production code mutates state only through QualType.

#include "types/type_id.hpp"

namespace trust {

enum class TypeFlag : uint64_t {
    Inferred = kInferredFlag,
    Const = kConstFlag,
};

constexpr TypeId setFlag(TypeId id, TypeFlag f) noexcept {
    return TypeId::fromRaw(id.raw() | static_cast<uint64_t>(f));
}
constexpr TypeId clearFlag(TypeId id, TypeFlag f) noexcept {
    return TypeId::fromRaw(id.raw() & ~static_cast<uint64_t>(f));
}
constexpr bool testFlag(TypeId id, TypeFlag f) noexcept {
    return (id.raw() & static_cast<uint64_t>(f)) != 0;
}

} // namespace trust
