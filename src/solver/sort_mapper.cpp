// src/solver/sort_mapper.cpp
// trust-solver: реализация SortMapper (см. include/solver/sort_mapper.hpp).

#include "solver/sort_mapper.hpp"

#include "diag/context.hpp"
#include "types/registry.hpp"
#include "types/typekind.hpp"
#include "types/group.hpp"

namespace trust {
namespace solver {

std::optional<TypeId> SortMapper::resolveTypeByName(std::string_view name) const {
    return m_ctx.types().findType(name);
}

std::optional<SmtSort> SortMapper::sortOf(TypeId id) const {
    if (id == INVALID_TYPE_ID) {
        return std::nullopt;
    }
    const TypeRegistry& reg = m_ctx.types();
    const TypeId canon = reg.getCanonicalTypeId(id);
    const TypeKind kind = getKindFromId(canon);
    const Group g = getGroup(kind);
    SmtSort s;
    switch (g) {
    case Group::kLogical:
        s.kind = SmtSortKind::kBool;
        return s;
    case Group::kIntegers:
    case Group::kUnsigned:
        s.kind = SmtSortKind::kBitVec;
        s.bv_width = static_cast<uint32_t>((kind & kTypeKindDataMask) >> kTypeKindDataShift);
        return s;
    case Group::kNumbers:
        s.kind = SmtSortKind::kReal;
        return s;
    default: {
        // Параметризованный структурный массив Array<Elem> → (Array (BitVec 64) elem).
        if (reg.isArrayType(canon)) {
            const ArrayTypeData* ad = reg.getTypeDataAs<ArrayTypeData>(canon);
            if (ad) {
                const auto elemSort = sortOf(ad->elementType);
                if (!elemSort) {
                    return std::nullopt;
                }
                SmtSort idx;
                idx.kind = SmtSortKind::kBitVec;
                idx.bv_width = 64;
                s.kind = SmtSortKind::kArray;
                s.domain = std::make_shared<SmtSort>(std::move(idx));
                s.range = std::make_shared<SmtSort>(*elemSort);
                return s;
            }
        }
        const TypeDescriptor* d = reg.lookup(canon);
        if (d) {
            s.kind = SmtSortKind::kUninterpreted;
            s.name = d->name;
            return s;
        }
        return std::nullopt;
    }
    }
}

bool SortMapper::isSignedType(TypeId id) const {
    if (id == INVALID_TYPE_ID) {
        return false; // неизвестен - zero-extend (безопаснее для беззнаковых)
    }
    const TypeRegistry& reg = m_ctx.types();
    const Group g = getGroup(getKindFromId(reg.getCanonicalTypeId(id)));
    return g == Group::kIntegers;
}

} // namespace solver
} // namespace trust
