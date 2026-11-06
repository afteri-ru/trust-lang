#pragma once

// Проверка C++-доступа к члену класса (private/protected) из текущего контекста анализа —
// ЕДИНАЯ точка для доступа к полю и вызова метода. Уровень члена берётся из реестра
// (`TupleElementData::access` / `CallableSignature::access`), куда его ставит семантика из
// `AstNodeBase::visibility` объявления.

#include "semantic/core/namespace_scope.hpp"
#include "semantic/core/pass.hpp"
#include "semantic/core/type_predicates.hpp"
#include "diag/diag.hpp"
#include "diag/severity.hpp"
#include "location/location.hpp"
#include "types/type_id.hpp"
#include "types/visibility.hpp"

#include <string_view>

namespace trust {

/// C++-доступ к члену: Public — всегда; Private — только из тела класса-владельца; Protected —
/// из тела владельца либо из наследника, но через объект типа, производного от наследника
/// (C++-правило). `objectType` — статический тип объекта доступа. false — доступ запрещён (ошибка).
inline bool checkMemberAccess(AnalysisContext& actx, MapperRange range, TypeId owner, TypeId objectType, Visibility access, std::string_view memberName) {
    if (access == Visibility::Public) {
        return true;
    }
    const TypeId current = actx.namespaces().currentTypeBodyId();
    const TypeReadModel& reg = actx.ctx().readTypes();
    bool allowed = current.isValid();
    if (allowed) {
        const bool sameOwner = reg.store().desugaredTypeId(current) == reg.store().desugaredTypeId(owner);
        allowed = access == Visibility::Private
                      ? sameOwner
                      : sameOwner || (isSubtypeOrEqual(reg, current, owner) && objectType.isValid() && isSubtypeOrEqual(reg, objectType, current));
    }
    if (allowed) {
        return true;
    }
    actx.ctx().diag().report(Severity::Error, range, "member '{}' of '{}' is {}", memberName, reg.printer().typeNameOf(owner),
                             access == Visibility::Private ? "private" : "protected");
    return false;
}

} // namespace trust
