#pragma once

#include "types/typekind.hpp"
#include "types/group.hpp"
#include "types/type_traits.hpp"

namespace trust {

// ── PromotionResult ──────────────────────────────────────
enum class PromotionResult : uint8_t {
    kSame,  // типы идентичны
    kOk,    // автоматическое продвижение (Int8 → Int32, Float32 → Float64)
    kCast,  // требуется явный каст
    kError, // каст невозможен
};

// ── Promotion chain for integer types ────────────────────
// Returns the next wider integer type, or 64-bit if already at max.
constexpr TypeKind promoteInteger(TypeKind k) noexcept {
    if (getGroup(k) != Group::kIntegers)
        return makeTypeKind(Group::kIntegers, 64);

    switch (getData(k)) {
    case 8:
        return makeTypeKind(Group::kIntegers, 16);
    case 16:
        return makeTypeKind(Group::kIntegers, 32);
    case 32:
    case 64:
        return makeTypeKind(Group::kIntegers, 64);
    default:
        return makeTypeKind(Group::kIntegers, 64);
    }
}

// ── Promotion chain for float types ──────────────────────
constexpr TypeKind promoteFloat(TypeKind k) noexcept {
    if (getGroup(k) != Group::kNumbers)
        return makeTypeKind(Group::kNumbers, 64);

    switch (getData(k)) {
    case 16:
        return makeTypeKind(Group::kNumbers, 32);
    case 32:
    case 64:
        return makeTypeKind(Group::kNumbers, 64);
    default:
        return makeTypeKind(Group::kNumbers, 64);
    }
}

// ── Common type for binary operations ────────────────────
constexpr PromotionResult getPromotion(TypeKind a, TypeKind b, TypeKind& common) noexcept {
    if (a == b) {
        common = a;
        return PromotionResult::kSame;
    }

    auto ga = getGroup(a);
    auto gb = getGroup(b);

    if (ga != gb) {
        if (ga == Group::kLogical || gb == Group::kLogical)
            return PromotionResult::kError;

        if (ga == Group::kRationals || gb == Group::kRationals)
            return PromotionResult::kError;

        if ((ga == Group::kComplex && gb == Group::kNumbers) || (ga == Group::kNumbers && gb == Group::kComplex))
            return PromotionResult::kError;

        if ((ga == Group::kUnsigned && gb == Group::kIntegers) || (ga == Group::kIntegers && gb == Group::kUnsigned))
            return PromotionResult::kCast;

        if (ga == Group::kStrChar || ga == Group::kStrWide || gb == Group::kStrChar || gb == Group::kStrWide)
            return PromotionResult::kCast;

        if (!belongsToCategory(ga, Category::kArithmetics) || !belongsToCategory(gb, Category::kArithmetics))
            return PromotionResult::kError;

        return PromotionResult::kCast;
    }

    auto da = getData(a);
    auto db = getData(b);

    if (da == db) {
        common = a;
        return PromotionResult::kSame;
    }

    if (da > db) {
        common = a;
    } else {
        common = b;
    }

    if (isBuiltinConcrete(a) && isBuiltinConcrete(b))
        return PromotionResult::kOk;

    return PromotionResult::kCast;
}

// ── Direct check ─────────────────────────────────────────
constexpr bool isPromotableTo(TypeKind from, TypeKind to) noexcept {
    TypeKind common;
    auto result = getPromotion(from, to, common);
    return result == PromotionResult::kSame || result == PromotionResult::kOk;
}

} // namespace trust
