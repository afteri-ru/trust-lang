#pragma once

#include <cstdint>
#include <functional>

#include "types/typekind.hpp"

namespace trust {

// ── TypeId ───────────────────────────────────────────────
// Полный идентификатор типа:
//   Upper 32 bits: TypeKind (битовая структура)
//   Lower 32 bits: registry_index (0 для встроенных типов)
using TypeId = uint64_t;

// ── Invalid type id ──────────────────────────────────────
static constexpr TypeId INVALID_TYPE_ID = 0;

// ── Construction ─────────────────────────────────────────
constexpr TypeId makeTypeId(TypeKind kind, uint32_t registry_index = 0) noexcept {
    return (static_cast<uint64_t>(kind) << 32) | registry_index;
}

// ── Field extraction ─────────────────────────────────────
constexpr TypeKind getKindFromId(TypeId id) noexcept {
    return static_cast<TypeKind>(id >> 32);
}

constexpr uint32_t getIndexFromId(TypeId id) noexcept {
    return static_cast<uint32_t>(id & 0xFFFFFFFFULL);
}

// ── Classification helpers ───────────────────────────────
constexpr bool isBuiltinTypeId(TypeId id) noexcept {
    return hasBuiltinFlag(getKindFromId(id));
}

constexpr bool isConcreteTypeId(TypeId id) noexcept {
    auto kind = getKindFromId(id);
    if (isBuiltinConcrete(kind))
        return true;
    return getIndexFromId(id) != 0;
}

// ── Hash (for use in unordered containers) ───────────────
struct TypeIdHash {
    uint64_t operator()(const TypeId& id) const noexcept { return id; }
};

} // namespace trust