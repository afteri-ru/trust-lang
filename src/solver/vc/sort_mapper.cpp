// trust-solver: реализация SortMapper (см. include/solver/vc/sort_mapper.hpp).

#include "solver/vc/sort_mapper.hpp"

#include "session/run_services.hpp"
#include "types/type_system.hpp"
#include "types/typekind.hpp"
#include "types/group.hpp"

namespace trust {
namespace solver {

std::optional<SmtSort> SortMapper::sortOf(TypeId id) const {
    if (id == INVALID_TYPE_ID) {
        return std::nullopt;
    }
    const TypeReadModel& reg = m_services.readTypes();
    const TypeId canon = reg.store().desugaredTypeId(id);
    const TypeKind kind = getTypeKind(canon);
    const Group g = typeGroupOf(kind);
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
        if (reg.store().isArrayType(canon)) {
            const ArrayTypeData* ad = reg.store().getTypeDataAs<ArrayTypeData>(canon);
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
        const std::string_view typeName = reg.store().nameOf(canon);
        if (!typeName.empty()) {
            s.kind = SmtSortKind::kUninterpreted;
            s.name = std::string(typeName);
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
    const TypeReadModel& reg = m_services.readTypes();
    const Group g = typeGroupOf(reg.store().desugaredTypeId(id));
    return g == Group::kIntegers;
}

std::optional<bool> SortMapper::integerSignOf(TypeId id) const {
    if (id == INVALID_TYPE_ID) {
        return std::nullopt;
    }
    const TypeReadModel& reg = m_services.readTypes();
    switch (typeGroupOf(reg.store().desugaredTypeId(id))) {
    case Group::kIntegers:
        return true;
    case Group::kUnsigned:
        return false;
    default:
        return std::nullopt;
    }
}

} // namespace solver
} // namespace trust
