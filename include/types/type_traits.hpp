#pragma once

#include "types/typekind.hpp"
#include "types/group.hpp"
#include "types/type_id.hpp"

namespace trust {

// ── Core classification ──────────────────────────────────

/// Data != 0 → builtin concrete type.
/// For registry types (Data=0), use isConcreteTypeId().
constexpr bool isConcrete(TypeKind k) noexcept {
    return isBuiltinConcrete(k);
}

// ── Category checks ──────────────────────────────────────

constexpr bool belongsToCategory(TypeKind k, Category c) noexcept {
    return belongsToCategory(getGroup(k), c);
}

// ── Arithmetic checks ────────────────────────────────────

constexpr bool isArithmetic(TypeKind k) noexcept {
    return belongsToCategory(k, Category::kArithmetics);
}

constexpr bool isInteger(TypeKind k) noexcept {
    return getGroup(k) == Group::kIntegers;
}

constexpr bool isUnsigned(TypeKind k) noexcept {
    return getGroup(k) == Group::kUnsigned;
}

constexpr bool isFloating(TypeKind k) noexcept {
    return getGroup(k) == Group::kNumbers;
}

constexpr bool isString(TypeKind k) noexcept {
    auto g = getGroup(k);
    return g == Group::kStrChar || g == Group::kStrWide;
}

constexpr bool isVoid(TypeKind k) noexcept {
    return getGroup(k) == Group::kVoid && getData(k) == 1;
}

constexpr bool isNone(TypeKind k) noexcept {
    return getGroup(k) == Group::kVoid && getData(k) == 2;
}

constexpr bool isBool(TypeKind k) noexcept {
    return getGroup(k) == Group::kLogical;
}

constexpr bool isRational(TypeKind k) noexcept {
    return getGroup(k) == Group::kRationals;
}

constexpr bool isComplex(TypeKind k) noexcept {
    return getGroup(k) == Group::kComplex;
}

constexpr bool isEllipsis(TypeKind k) noexcept {
    return getGroup(k) == Group::kEllipsis;
}

// ── Size (in bytes) for builtin types ────────────────────
// For builtin concrete types, data field = bit width.
// Returns 0 for abstract / registry types.

constexpr uint8_t typeSize(TypeKind k) noexcept {
    if (!isBuiltinConcrete(k))
        return 0;
    auto d = getData(k);
    return d / 8;
}

} // namespace trust
