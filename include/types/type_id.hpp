#pragma once

#include <cstdint>
#include <functional>

#include "types/typekind.hpp"

namespace trust {

// -- TypeId -----------------------------------------------
// Полный идентификатор типа:
//   Upper 32 bits: TypeKind (битовая структура)
//   Lower 32 bits: registry_index (0 для встроенных типов)
using TypeId = uint64_t;

// -- Invalid type id --------------------------------------
static constexpr TypeId INVALID_TYPE_ID = 0;

// -- Флаг «тип выведен автоматически» (kInferredFlag) -----
// Бит 31 младшей половины TypeId. Структурная идентичность типа интернируется на 63 битах
// (TypeKind в старших 32 + registry_index в младших 31); этот бит - ортогональный квалификатор
// «тип выведен автоматически (из литерала / inferred-переменной)», а НЕ часть ключа
// интернирования. Все операции идентичности (getIndexFromId, getCanonicalTypeId, сравнения
// каноникой) снимают его; маскирование сосредоточено в реестре (см. types/registry.hpp).
constexpr uint64_t kInferredFlag = 0x80000000ULL; // bit 31 нижней (registry_index) половины
constexpr TypeId withInferred(TypeId id) noexcept {
    return id | kInferredFlag;
}
constexpr bool typeIsInferred(TypeId id) noexcept {
    return (id & kInferredFlag) != 0;
}
constexpr TypeId clearInferred(TypeId id) noexcept {
    return id & ~kInferredFlag;
}

// -- Флаг «константность значения/переменной» (kConstFlag) -
// Бит 30 младшей половины TypeId. Ортогональный квалификатор константности (неизменяемости)
// значения/переменной - по аналогии с kInferredFlag. Может быть установлен двумя способами:
//  1) «константность в типе» - на самом типе (декларация `x^ := 42` → тип `const T`); тогда
//     getCppTypeName даёт префикс `const ` и константность попадает в прототипы функций;
//  2) «пер-переменная константность» - на Symbol::type по мере анализа узлов AST (аналог
//     top-level const / Rust `let`): в структурную идентичность и сигнатуры функций НЕ входит,
//     а при кодогенерации выражается через const_cast<>, когда сам тип не константный.
// НЕ часть ключа интернирования: структурные операции (getIndexFromId, getCanonicalTypeId)
// снимают его (см. types/registry.hpp, types/MEMORY.md).
constexpr uint64_t kConstFlag = 0x40000000ULL; // bit 30 нижней (registry_index) половины
constexpr TypeId withConst(TypeId id) noexcept {
    return id | kConstFlag;
}
constexpr bool typeIsConst(TypeId id) noexcept {
    return (id & kConstFlag) != 0;
}
constexpr TypeId clearConst(TypeId id) noexcept {
    return id & ~kConstFlag;
}

// -- Construction -----------------------------------------
constexpr TypeId makeTypeId(TypeKind kind, uint32_t registry_index = 0) noexcept {
    return (static_cast<uint64_t>(kind) << 32) | registry_index;
}

// -- Field extraction -------------------------------------
constexpr TypeKind getKindFromId(TypeId id) noexcept {
    return static_cast<TypeKind>(id >> 32);
}

constexpr uint32_t getIndexFromId(TypeId id) noexcept {
    // Снимаем kInferredFlag и kConstFlag: registry_index структурный, признаки в индекс не входят.
    return static_cast<uint32_t>(id & ~kInferredFlag & ~kConstFlag);
}

// -- Флаг «тип несёт trust-условия» (kTrustFlag) -----------
// Бит в TypeKind (верхняя половина TypeId, Reserved). Это СЕМАНТИЧЕСКИЙ дифференциатор
// идентичности: тип/функция с пред-/пост-условиями/утверждениями не эквивалентен идентичному
// без условий. В отличие от kInferred/kConst (нижняя половина, квалификаторы вхождения),
// бит НЕ снимается getIndexFromId/getCanonicalTypeId (маскируют нижнюю половину) и входит в
// ключи структурного интернирования (TypeKey::kind). Используется для защиты от автоматического
// вывода типа: переменная, чей выведенный тип несёт trust-условия, обязана иметь явную
// аннотацию типа (см. types/MEMORY.md, семантика analyzeVarDecl/typeExpr).
constexpr bool typeIsTrusted(TypeId id) noexcept {
    return hasTrustFlag(getKindFromId(id));
}
constexpr TypeId withTrusted(TypeId id) noexcept {
    const uint64_t lower = id & 0xFFFFFFFFULL; // сохраняем registry_index + нижние квалификаторы
    return makeTypeId(setTrustFlag(getKindFromId(id)), static_cast<uint32_t>(lower));
}

// -- Classification helpers -------------------------------
constexpr bool isBuiltinTypeId(TypeId id) noexcept {
    return hasBuiltinFlag(getKindFromId(id));
}

constexpr bool isConcreteTypeId(TypeId id) noexcept {
    auto kind = getKindFromId(id);
    if (isBuiltinConcrete(kind)) {
        return true;
    }
    return getIndexFromId(id) != 0;
}

// -- Hash (for use in unordered containers) ---------------
struct TypeIdHash {
    uint64_t operator()(const TypeId& id) const noexcept { return id; }
};

} // namespace trust