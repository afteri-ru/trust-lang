#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace trust {

// -- Forward declarations --------------------------------
enum class Group : uint8_t;
enum class TypeClass : uint8_t;
enum class RefType : uint8_t;

// -- SizeUnit (bits or bytes) ----------------------------
enum class SizeUnit : uint8_t {
    kBits = 0,
    kBytes = 1,
};

// -- TypeKind ---------------------------------------------
// 32-bit: Group(8) + Data(8) + RefType(4) + TypeClass(2) + SizeUnit(1) + Reserved(9)
using TypeKind = uint32_t;

// -- Bit positions ----------------------------------------
constexpr uint32_t kTypeKindGroupShift = 0;
constexpr uint32_t kTypeKindGroupMask = 0xFFU << kTypeKindGroupShift;

constexpr uint32_t kTypeKindDataShift = 8;
constexpr uint32_t kTypeKindDataMask = 0xFFU << kTypeKindDataShift;

constexpr uint32_t kTypeKindRefTypeShift = 16;
constexpr uint32_t kTypeKindRefTypeMask = 0xFU << kTypeKindRefTypeShift;

constexpr uint32_t kTypeKindClassShift = 20;
constexpr uint32_t kTypeKindClassMask = 0x3U << kTypeKindClassShift;

constexpr uint32_t kTypeKindSizeUnitShift = 22;
constexpr uint32_t kTypeKindSizeUnitMask = 0x1U << kTypeKindSizeUnitShift;

constexpr uint32_t kTypeKindReservedShift = 23;
constexpr uint32_t kTypeKindReservedMask = 0x1FFU << kTypeKindReservedShift;

// -- Builtin flag ------------------------------------------
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

// -- Trust flag ------------------------------------------------
// Бит «тип несёт trust-условия» (пред-/пост-условия, утверждения). СЕМАНТИЧЕСКИЙ
// дифференциатор идентичности типа: тип/функция с условиями НЕ эквивалентен идентичному без
// условий. Поэтому бит живёт в TypeKind (верхняя половина TypeId) и, как следствие, входит в
// ключи структурного интернирования (TypeKey::kind) - интернируются раздельно. В отличие от
// kConst/kInferred (квалификаторы вхождения, нижняя половина) НЕ снимается канонизацией и
// масками нижних бит. Устанавливается ЯВНО при регистрации пользовательского типа/функции
// с непустым m_trust; в контейнерные/структурные типы НЕ пропагируется автоматически.
// Используется для защиты от автоматического вывода типа (см. type_id.hpp).
constexpr uint32_t kTypeKindTrustFlag = 0x1U << 24;

constexpr TypeKind setTrustFlag(TypeKind k) noexcept {
    return static_cast<TypeKind>(static_cast<uint32_t>(k) | kTypeKindTrustFlag);
}

constexpr bool hasTrustFlag(TypeKind k) noexcept {
    return (static_cast<uint32_t>(k) & kTypeKindTrustFlag) != 0;
}
// -- TypeClass --------------------------------------------
enum class TypeClass : uint8_t {
    kTrivial = 0,     // memcpy ok, no ctor/dtor
    kRelocatable = 1, // memcpy + destroy old
    kComplex = 2,     // full ctor/dtor/move
    kPolymorphic = 3, // vtable, dynamic_cast
};

// -- RefType ----------------------------------------------
// Плоский enum «вид ссылки». ОДИН признак ссылки на объявление - осознанное решение
// для упрощения понимания системы ссылочных типов (НЕ следствие 4-битного поля),
// подробно: types/REFType.md. Первая ссылка на тип без признака - fast-path бит
// (withRefType); для вложенности (ссылку на уже ссылочный тип) создаётся составной
// узел getOrCreateRefType (types/registry.hpp). Сырые C++-виды (ptr/ref/rref/mptr/ptrptr)
// напрямую операторами не используются - только через атрибут `@[reftype("...")]`;
// классические операторы дают безопасные виды (value/shared/weak/unique).
enum class RefType : uint8_t {
    kValue = 0,  // value  - владение значением (без ссылки)
    kShared = 1, // shared - совместное владение
    kWeak = 2,   // weak   - слабая (не владеющая) ссылка
    kUnique = 3, // unique - исключительное владение
    kPtr = 4,    // ptr    - сырой указатель (*), только через атрибут
    kMptr = 5,   // mptr   - указатель на член (::*), MemberPointerTypeData
    kRef = 6,    // ref    - ссылка (&), только через атрибут
    kRref = 7,   // rref   - rvalue-ссылка (&&), только через атрибут
    kPtrPtr = 8, // ptrptr - указатель на указатель (**)
    kTake = 9,   // take   - владеющая в рамках текущего скоупа (RAII-охранник, результат take)
    // 10-15 reserved
};

// -- Строковые имена видов ссылок (для @[reftype("...")] и диагностики) --
[[nodiscard]] constexpr std::string_view refTypeName(RefType r) noexcept {
    switch (r) {
    case RefType::kValue:
        return "value";
    case RefType::kShared:
        return "shared";
    case RefType::kWeak:
        return "weak";
    case RefType::kUnique:
        return "unique";
    case RefType::kPtr:
        return "ptr";
    case RefType::kMptr:
        return "mptr";
    case RefType::kRef:
        return "ref";
    case RefType::kRref:
        return "rref";
    case RefType::kPtrPtr:
        return "ptrptr";
    case RefType::kTake:
        return "take";
    }
    return "unknown";
}

// Обратный маппинг строки → RefType. Неизвестное имя → std::nullopt (вызывающая
// сторона обязана выдать диагностику, см. AGENTS п.5: без тихого fallback).
[[nodiscard]] inline std::optional<RefType> refTypeFromString(std::string_view s) noexcept {
    if (s == "value") {
        return RefType::kValue;
    }
    if (s == "shared") {
        return RefType::kShared;
    }
    if (s == "weak") {
        return RefType::kWeak;
    }
    if (s == "unique") {
        return RefType::kUnique;
    }
    if (s == "ptr") {
        return RefType::kPtr;
    }
    if (s == "mptr") {
        return RefType::kMptr;
    }
    if (s == "ref") {
        return RefType::kRef;
    }
    if (s == "rref") {
        return RefType::kRref;
    }
    if (s == "ptrptr") {
        return RefType::kPtrPtr;
    }
    if (s == "take") {
        return RefType::kTake;
    }
    return std::nullopt;
}

// -- Construction -----------------------------------------
constexpr TypeKind makeTypeKind(Group group, uint8_t data, TypeClass tc = TypeClass::kTrivial, RefType ref = RefType::kValue,
                                SizeUnit su = SizeUnit::kBits) noexcept {
    auto raw = static_cast<uint32_t>(static_cast<uint8_t>(group)) | (static_cast<uint32_t>(data) << kTypeKindDataShift) |
               (static_cast<uint32_t>(ref) << kTypeKindRefTypeShift) | (static_cast<uint32_t>(tc) << kTypeKindClassShift) |
               (static_cast<uint32_t>(su) << kTypeKindSizeUnitShift);
    return static_cast<TypeKind>(raw);
}

// -- Field extraction -------------------------------------
constexpr Group getGroup(TypeKind k) noexcept {
    return static_cast<Group>(static_cast<uint32_t>(k) & kTypeKindGroupMask);
}

constexpr uint8_t getData(TypeKind k) noexcept {
    return static_cast<uint8_t>((static_cast<uint32_t>(k) & kTypeKindDataMask) >> kTypeKindDataShift);
}

constexpr RefType getRefType(TypeKind k) noexcept {
    return static_cast<RefType>((static_cast<uint32_t>(k) & kTypeKindRefTypeMask) >> kTypeKindRefTypeShift);
}

constexpr TypeClass getTypeClass(TypeKind k) noexcept {
    return static_cast<TypeClass>((static_cast<uint32_t>(k) & kTypeKindClassMask) >> kTypeKindClassShift);
}

constexpr SizeUnit getSizeUnit(TypeKind k) noexcept {
    return static_cast<SizeUnit>((static_cast<uint32_t>(k) & kTypeKindSizeUnitMask) >> kTypeKindSizeUnitShift);
}

// -- Field setting (returns new TypeKind) ------------------
constexpr TypeKind withRefType(TypeKind k, RefType ref) noexcept {
    auto raw = (static_cast<uint32_t>(k) & ~kTypeKindRefTypeMask) | (static_cast<uint32_t>(ref) << kTypeKindRefTypeShift);
    return static_cast<TypeKind>(raw);
}

// -- Classification helpers -------------------------------
// Data != 0 → builtin concrete type (can be stored as value)
constexpr bool isBuiltinConcrete(TypeKind k) noexcept {
    return getData(k) != 0;
}

} // namespace trust