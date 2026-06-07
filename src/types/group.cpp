#include "types/group.hpp"

namespace trust {

// ── Group → Category mask table ──────────────────────────
// Indexed by static_cast<uint8_t>(Group).
// Each value is a bitmask of Category bits for that group.

const CategoryMask kGroupCategoryMask[256] = {
    // Group::kAny (0)
    (1u << static_cast<uint8_t>(Category::kAny)),

    // Group::kVoid (1)
    (1u << static_cast<uint8_t>(Category::kVoid)),

    // Group::kLogical (2)
    (1u << static_cast<uint8_t>(Category::kArithmetics)),

    // Group::kIntegers (3)
    (1u << static_cast<uint8_t>(Category::kArithmetics)),

    // Group::kUnsigned (4)
    (1u << static_cast<uint8_t>(Category::kArithmetics)),

    // Group::kNumbers (5)
    (1u << static_cast<uint8_t>(Category::kArithmetics)),

    // Group::kBFloat (6)
    (1u << static_cast<uint8_t>(Category::kArithmetics)),

    // Group::kComplex (7)
    (1u << static_cast<uint8_t>(Category::kArithmetics)),

    // Group::kRationals (8)
    (1u << static_cast<uint8_t>(Category::kArithmetics)),

    // Group::kStrChar (9)
    (1u << static_cast<uint8_t>(Category::kStrings)),

    // Group::kStrWide (10)
    (1u << static_cast<uint8_t>(Category::kStrings)),

    // Group::kTensors (11)
    (1u << static_cast<uint8_t>(Category::kTensors)),

    // Group::kContainers (12)
    (1u << static_cast<uint8_t>(Category::kContainers)),

    // Group::kStructured (13)
    (1u << static_cast<uint8_t>(Category::kStructured)),

    // Group::kCallable (14)
    (1u << static_cast<uint8_t>(Category::kCallable)),

    // Group::kClasses (15)
    (1u << static_cast<uint8_t>(Category::kClasses)),

    // Group::kRanges (16)
    (1u << static_cast<uint8_t>(Category::kRanges)),

    // Group::kIterators (17)
    (1u << static_cast<uint8_t>(Category::kIterators)),

    // Group::kDateTime (18)
    (1u << static_cast<uint8_t>(Category::kDateTime)),

    // Group::kAsync (19)
    (1u << static_cast<uint8_t>(Category::kAsync)),

    // Group::kSync (20)
    (1u << static_cast<uint8_t>(Category::kSync)),

    // Group::kExceptions (21)
    (1u << static_cast<uint8_t>(Category::kExceptions)),

    // Group::kNative (22)
    (1u << static_cast<uint8_t>(Category::kNative)),

    // Group::kEllipsis (23)
    (1u << static_cast<uint8_t>(Category::kEllipsis)),

    // Group::kArithmetics (24)
    (1u << static_cast<uint8_t>(Category::kArithmetics)),

    // The rest are implicitly zero (no category)
    // Up to index 255
};

static_assert(static_cast<uint8_t>(Group::kArithmetics) == 24, "Group enum values have changed — update kGroupCategoryMask");

} // namespace trust
