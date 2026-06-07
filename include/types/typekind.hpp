#pragma once

#include <cstdint>
#include <string_view>

namespace trust {

// ── Forward declarations ────────────────────────────────
enum class Group : uint8_t;
enum class TypeClass : uint8_t;
enum class RefKind : uint8_t;

// ── SizeUnit (bits or bytes) ────────────────────────────
enum class SizeUnit : uint8_t {
    kBits = 0,
    kBytes = 1,
};

// ── TypeKind ─────────────────────────────────────────────
// 32-bit: Group(8) + Data(8) + RefKind(4) + TypeClass(2) + SizeUnit(1) + Reserved(9)
using TypeKind = uint32_t;

// ── Bit positions ────────────────────────────────────────
constexpr uint32_t kTypeKindGroupShift = 0;
constexpr uint32_t kTypeKindGroupMask = 0xFFU << kTypeKindGroupShift;

constexpr uint32_t kTypeKindDataShift = 8;
constexpr uint32_t kTypeKindDataMask = 0xFFU << kTypeKindDataShift;

constexpr uint32_t kTypeKindRefKindShift = 16;
constexpr uint32_t kTypeKindRefKindMask = 0xFU << kTypeKindRefKindShift;

constexpr uint32_t kTypeKindClassShift = 20;
constexpr uint32_t kTypeKindClassMask = 0x3U << kTypeKindClassShift;

constexpr uint32_t kTypeKindSizeUnitShift = 22;
constexpr uint32_t kTypeKindSizeUnitMask = 0x1U << kTypeKindSizeUnitShift;

constexpr uint32_t kTypeKindReservedShift = 23;
constexpr uint32_t kTypeKindReservedMask = 0x1FFU << kTypeKindReservedShift;

// ── Builtin flag ──────────────────────────────────────────
// Бит флага "встроенный тип". Устанавливается registerBuiltinType(),
// позволяет определить builtin-тип по TypeKind без доступа к TypeRegistry.
constexpr uint32_t kTypeKindBuiltinFlag = 0x1U << kTypeKindReservedShift;
constexpr uint32_t kTypeKindBuiltinMask = kTypeKindBuiltinFlag;

constexpr TypeKind setBuiltinFlag(TypeKind k) noexcept {
    return static_cast<TypeKind>(static_cast<uint32_t>(k) | kTypeKindBuiltinFlag);
}

constexpr bool hasBuiltinFlag(TypeKind k) noexcept {
    return (static_cast<uint32_t>(k) & kTypeKindBuiltinFlag) != 0;
}
// ── TypeClass ────────────────────────────────────────────
enum class TypeClass : uint8_t {
    kTrivial = 0,     // memcpy ok, no ctor/dtor
    kRelocatable = 1, // memcpy + destroy old
    kComplex = 2,     // full ctor/dtor/move
    kPolymorphic = 3, // vtable, dynamic_cast
};

// ── RefKind ──────────────────────────────────────────────
enum class RefKind : uint8_t {
    kNone = 0,   // value (owning)
    kShared = 1, // shared ownership
    kWeak = 2,   // weak reference (non-owning)
    kUnique = 3, // unique (exclusive) ownership
    // 4-15 reserved for future use
};

// ── Construction ─────────────────────────────────────────
constexpr TypeKind makeTypeKind(Group group, uint8_t data, TypeClass tc = TypeClass::kTrivial, RefKind ref = RefKind::kNone,
                                SizeUnit su = SizeUnit::kBits) noexcept {
    auto raw = static_cast<uint32_t>(static_cast<uint8_t>(group)) | (static_cast<uint32_t>(data) << kTypeKindDataShift) |
               (static_cast<uint32_t>(ref) << kTypeKindRefKindShift) | (static_cast<uint32_t>(tc) << kTypeKindClassShift) |
               (static_cast<uint32_t>(su) << kTypeKindSizeUnitShift);
    return static_cast<TypeKind>(raw);
}

// ── Field extraction ─────────────────────────────────────
constexpr Group getGroup(TypeKind k) noexcept {
    return static_cast<Group>(static_cast<uint32_t>(k) & kTypeKindGroupMask);
}

constexpr uint8_t getData(TypeKind k) noexcept {
    return static_cast<uint8_t>((static_cast<uint32_t>(k) & kTypeKindDataMask) >> kTypeKindDataShift);
}

constexpr RefKind getRefKind(TypeKind k) noexcept {
    return static_cast<RefKind>((static_cast<uint32_t>(k) & kTypeKindRefKindMask) >> kTypeKindRefKindShift);
}

constexpr TypeClass getTypeClass(TypeKind k) noexcept {
    return static_cast<TypeClass>((static_cast<uint32_t>(k) & kTypeKindClassMask) >> kTypeKindClassShift);
}

constexpr SizeUnit getSizeUnit(TypeKind k) noexcept {
    return static_cast<SizeUnit>((static_cast<uint32_t>(k) & kTypeKindSizeUnitMask) >> kTypeKindSizeUnitShift);
}

// ── Field setting (returns new TypeKind) ──────────────────
constexpr TypeKind withRefKind(TypeKind k, RefKind ref) noexcept {
    auto raw = (static_cast<uint32_t>(k) & ~kTypeKindRefKindMask) | (static_cast<uint32_t>(ref) << kTypeKindRefKindShift);
    return static_cast<TypeKind>(raw);
}

// ── Classification helpers ───────────────────────────────
// Data != 0 → builtin concrete type (can be stored as value)
constexpr bool isBuiltinConcrete(TypeKind k) noexcept {
    return getData(k) != 0;
}

} // namespace trust