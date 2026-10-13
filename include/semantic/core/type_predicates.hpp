#pragma once

// include/semantic/core/type_predicates.hpp
// Единые предикаты типа для диагностик контракта value/reference и словарного операнда.

#include "types/names.hpp"
#include "types/type_id.hpp"
#include "types/type_read.hpp"
#include "types/typekind.hpp"

namespace trust {

// :Dict по каноническому id (spread-merge `[]= ... dict`).
inline bool isDictTypeId(const TypeReadModel& reg, TypeId tid) noexcept {
    if (tid == INVALID_TYPE_ID) {
        return false;
    }
    return reg.store().desugaredTypeId(tid) == reg.getType(type::Dict);
}

// Тип несёт признак ссылки (отличный от kValue); учитывает составные RefTypeData.
inline bool isRefTypeId(TypeId tid) noexcept {
    if (tid == INVALID_TYPE_ID) {
        return false;
    }
    return getRefType(getTypeKind(tid)) != RefType::kValue;
}

} // namespace trust
