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

// -- Флаг «инициализирована/не инициализирована» (kUninitFlag) --
// Бит 29 младшей половины TypeId. Ортогональный квалификатор ПО АНАЛОГИИ с пер-переменной
// константностью (второй вариант kConstFlag): устанавливается на Symbol::type по мере анализа
// узлов AST (объявление `x := _;` / сброс `x = _;` → «не инициализирована»; обычная запись
// `x = <expr>` → «инициализирована»). В структурную идентичность, сигнатуры и вывод типа НЕ
// входит и на кодогенерацию напрямую не влияет: структурные операции (getIndexFromId,
// getCanonicalTypeId) снимают его (см. types/registry.hpp, types/MEMORY.md). Признак - свойство
// конкретного Symbol (его тип-копия несёт бит), а не «const/trust в типе».
constexpr uint64_t kUninitFlag = 0x20000000ULL; // bit 29 нижней (registry_index) половины

// -- Слой поведенческих флагов (единый интерфейс пер-Symbol флагов) ----------
// TypeId - КОНТЕЙНЕР: { структурный тип (TypeKind + registry_index) | поведенческие флаги
// (kSymbolFlagsMask - зарезервированные биты младшей половины) }. Сам u64 пригоден как носитель
// и в рантайме. Флаги - НЕ семантика типа (см. types/MEMORY.md): работа с ТИПОМ и работа с
// ФЛАГАМИ - РАЗНЫЕ слои. Тип наружу выдаётся только через structuralType()/канонизаторы
// (снимают маску), а флаги читаются/пишутся только setFlag/clearFlag/testFlag на конкретном
// Symbol (НЕ через resolvedType). Разрозненные clearInferred/clearConst/clearUninit по месту
// потребителей запрещены - всё через эти единые функции слоя.
constexpr uint64_t kSymbolFlagsMask = kInferredFlag | kConstFlag | kUninitFlag;

enum class SymbolFlag : uint64_t {
    Inferred = kInferredFlag, ///< тип выведен автоматически
    Const = kConstFlag,       ///< пер-переменная константность (ReadOnly, 2-й вариант)
    Uninit = kUninitFlag,     ///< переменная не инициализирована (`x := _;` / сброс `x = _;`)
};

/// Снять ВСЕ поведенческие флаги (вернуть чистый тип-часть контейнера).
constexpr TypeId clearSymbolFlags(TypeId id) noexcept {
    return id & ~kSymbolFlagsMask;
}
constexpr TypeId setFlag(TypeId id, SymbolFlag f) noexcept {
    return id | static_cast<uint64_t>(f);
}
constexpr TypeId clearFlag(TypeId id, SymbolFlag f) noexcept {
    return id & ~static_cast<uint64_t>(f);
}
constexpr bool testFlag(TypeId id, SymbolFlag f) noexcept {
    return (id & static_cast<uint64_t>(f)) != 0;
}
/// Тип-часть контейнера: structuralType снимает ВСЕ поведенческие флаги (type не несёт флагов;
/// используется для сравнений, интернирования, emit).
constexpr TypeId structuralType(TypeId id) noexcept {
    return clearSymbolFlags(id);
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
    // Снимаем ВСЕ поведенческие флаги (kSymbolFlagsMask): registry_index структурный, флаги в индекс не входят.
    return static_cast<uint32_t>(clearSymbolFlags(id));
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