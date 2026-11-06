#pragma once

// Единые предикаты типа: связь по контракту value/reference, словарный операнд, отношение
// «подтип-или-равно» по базовым классам.

#include "types/group.hpp"
#include "types/inheritance.hpp"
#include "types/names.hpp"
#include "types/type_id.hpp"
#include "types/type_read.hpp"
#include "types/typekind.hpp"

#include <algorithm>
#include <vector>

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
    return typeRefOf(tid) != RefType::kValue;
}

// Транзитивное «подтип-или-равно» по базовым классам реестра (с защитой от циклов).
// Абстрактная группа-цель (Data==0, кроме Any) сравнивается по совпадению группы.
inline bool isSubtypeOrEqual(const TypeReadModel& reg, TypeId type, TypeId target) {
    const TypeId t = (type == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.store().identityOf(type, TypeIdentityScope::kCanonical);
    const TypeId g = (target == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.store().identityOf(target, TypeIdentityScope::kCanonical);
    if (t == INVALID_TYPE_ID || g == INVALID_TYPE_ID) {
        return false;
    }
    if (t == g) {
        return true;
    }
    const TypeKind tk = getTypeKind(g);
    if (typeDataOf(tk) == 0 && typeGroupOf(tk) != Group::kAuto) {
        return typeGroupOf(t) == typeGroupOf(tk);
    }
    std::vector<TypeId> stack;
    for (const BaseClass& b : reg.store().basesOf(t)) {
        stack.push_back(b.type);
    }
    std::vector<TypeId> seen;
    while (!stack.empty()) {
        const TypeId b = stack.back();
        stack.pop_back();
        const TypeId bc = (b == INVALID_TYPE_ID) ? INVALID_TYPE_ID : reg.store().identityOf(b, TypeIdentityScope::kCanonical);
        if (bc == INVALID_TYPE_ID || std::find(seen.begin(), seen.end(), bc) != seen.end()) {
            continue;
        }
        if (bc == g) {
            return true;
        }
        seen.push_back(bc);
        for (const BaseClass& nb : reg.store().basesOf(bc)) {
            stack.push_back(nb.type);
        }
    }
    return false;
}

} // namespace trust
