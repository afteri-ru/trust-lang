#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace trust {

// -- Forward declarations --------------------------------
enum class Group : uint8_t;
enum class TypeClass : uint8_t;
// ИНВАРИАНТ (двухосевая модель): RefType смешивает две независимые оси - ВЛАДЕНИЕ временем
// жизни (value/shared/weak/unique) и ДОСТУП к объекту (сырой `&`/`*` против охраняемого
// `locker`). `kLocker` - охраняемый доступ ТОЛЬКО к reference-wrapper (trust::Shared/Weak,
// результат lock()/lock_const()); сырые ссылки (ptr/ref/rref) и эксклюзивное владение локера
// не имеют (другая идеология - прямой доступ без guard'а). Null-безопасность - отдельная ось
// (контракт типа), а не guard-объект.
//
// ИНВАРИАНТ (одноуровневое пересечение в операторе): многоуровневая ссылочность как ТИП
// разрешена (каждый уровень - отдельно объявленный тип, напр. `Locker<Locker<T>>`), НО
// пересечение нескольких уровней в ОДНОМ операторе/инструкции запрещено: каждый уровень
// обрабатывается отдельной операцией над отдельным типом (сначала один, потом следующий).
// Цепочечного `**`/`ref.lock().lock()` в одном выражении нет.
//

// -- TypeKind: uint32_t, упакованные характеристики типа (битовая структура - types/TYPE.md) --
using TypeKind = uint32_t;

// Сдвиги/маски битовых полей TypeKind (TYPE.md «Битовая структура TypeKind»):
//   [0-7] Group | [8-15] Data | [16-19] RefType | [20-21] TypeClass | [22] SizeUnit |
//   [23] BuiltinFlag | [24] TrustFlag | [25] HasAttrsFlag | [26-31] reserved
constexpr uint32_t kTypeKindGroupShift = 0;
constexpr uint32_t kTypeKindGroupMask = 0x000000FFu;
constexpr uint32_t kTypeKindDataShift = 8;
constexpr uint32_t kTypeKindDataMask = 0x0000FF00u;
constexpr uint32_t kTypeKindRefTypeShift = 16;
constexpr uint32_t kTypeKindRefTypeMask = 0x000F0000u;
constexpr uint32_t kTypeKindClassShift = 20;
constexpr uint32_t kTypeKindClassMask = 0x00300000u;
constexpr uint32_t kTypeKindSizeUnitShift = 22;
constexpr uint32_t kTypeKindSizeUnitMask = 0x00400000u;
constexpr uint32_t kTypeKindBuiltinFlagShift = 23;
constexpr uint32_t kTypeKindBuiltinFlagMask = 0x00800000u;
constexpr uint32_t kTypeKindTrustFlagShift = 24;
constexpr uint32_t kTypeKindTrustFlagMask = 0x01000000u;
// HasAttrsFlag: признак «тип несёт атрибуты» (привязка к типу в реестре). Используется как
// fast-path-сигнал при сравнении типов: если у ОБОИХ типов флага нет, сравнение по TypeId
// (без реестра); если флаг есть хоть у одного - требуется доступ к TypeRegistry
// (TypeRegistry::typesEqual). Аналогичен kTrustFlag (семантический дифференциатор).
constexpr uint32_t kTypeKindAttrsFlagShift = 25;
constexpr uint32_t kTypeKindAttrsFlagMask = 0x02000000u;

// -- TypeClass (0..3) --------------------------------------
enum class TypeClass : uint8_t {
    kTrivial = 0,     // trivially copyable/relocatable
    kRelocatable = 1, // relocatable (move = memcpy)
    kComplex = 2,     // non-trivial copy/move
    kPolymorphic = 3, // virtual (polymorphic)
};

// -- SizeUnit (0..1) ---------------------------------------
enum class SizeUnit : uint8_t {
    kBits = 0, // Data - в битах
    kBytes = 1 // Data - в байтах
};

// -- Builtin / Trust flag helpers --------------------------
constexpr bool hasBuiltinFlag(TypeKind k) noexcept {
    return (k & kTypeKindBuiltinFlagMask) != 0;
}
constexpr TypeKind setBuiltinFlag(TypeKind k) noexcept {
    return static_cast<TypeKind>(k | kTypeKindBuiltinFlagMask);
}
constexpr bool hasTrustFlag(TypeKind k) noexcept {
    return (k & kTypeKindTrustFlagMask) != 0;
}
constexpr TypeKind setTrustFlag(TypeKind k) noexcept {
    return static_cast<TypeKind>(k | kTypeKindTrustFlagMask);
}
// HasAttrsFlag - признак «тип несёт атрибуты» (быстрый путь при сравнении типов).
constexpr bool hasAttrsFlag(TypeKind k) noexcept {
    return (k & kTypeKindAttrsFlagMask) != 0;
}
constexpr TypeKind setAttrsFlag(TypeKind k) noexcept {
    return static_cast<TypeKind>(k | kTypeKindAttrsFlagMask);
}

// X-macro: единый источник для вида ссылки (RefType). Каждая запись несёт:
//   (kind, мнемоническое имя, значение бита, C++-имя шаблона-обёртки).
// Мнемоническое имя используется в `@[reftype("...")]` и диагностике; C++-имя обёртки
// (trust::Shared / trust::Weak / trust::Locker / trust::Unique) - в кодогенерации
// getCppTypeName (registry.cpp), чтобы НЕ хардкодить имя класса вручную.
// Для не-обёрточных видов (value/ptr/mptr/ref/rref/ptrptr) C++-имя шаблона пустое.
#define TRUST_REF_TYPE_TYPES(X)              \
    X(kValue, "value", 0, /*cpp*/ "")        \
    X(kShared, "shared", 1, "trust::Shared") \
    X(kWeak, "weak", 2, "trust::Weak")       \
    X(kUnique, "unique", 3, "trust::Unique") \
    X(kPtr, "ptr", 4, /*cpp*/ "")            \
    X(kMptr, "mptr", 5, /*cpp*/ "")          \
    X(kRef, "ref", 6, /*cpp*/ "")            \
    X(kRref, "rref", 7, /*cpp*/ "")          \
    X(kPtrPtr, "ptrptr", 8, /*cpp*/ "")      \
    X(kLocker, "locker", 9, "trust::Locker")

enum class RefType : uint8_t {
#define TRUST_REF_TYPE_GEN_ENUM(kind, name, value, cpp) kind = value,
    TRUST_REF_TYPE_TYPES(TRUST_REF_TYPE_GEN_ENUM)
#undef TRUST_REF_TYPE_GEN_ENUM
};

// -- Строковое имя вида ссылки (для @[reftype("...")] и диагностики) --
[[nodiscard]] constexpr std::string_view refTypeName(RefType r) noexcept {
    switch (r) {
#define TRUST_REF_TYPE_GEN_NAME(kind, name, value, cpp) \
    case RefType::kind:                                 \
        return name;
        TRUST_REF_TYPE_TYPES(TRUST_REF_TYPE_GEN_NAME)
#undef TRUST_REF_TYPE_GEN_NAME
    }
    return "unknown";
}

// -- C++-имя шаблона-обёртки (для кодогена getCppTypeName); пусто для не-обёрточных видов --
[[nodiscard]] constexpr std::string_view refTypeCppTemplateName(RefType r) noexcept {
    switch (r) {
#define TRUST_REF_TYPE_GEN_CPP(kind, name, value, cpp) \
    case RefType::kind:                                \
        return cpp;
        TRUST_REF_TYPE_TYPES(TRUST_REF_TYPE_GEN_CPP)
#undef TRUST_REF_TYPE_GEN_CPP
    }
    return "";
}

// Обратный маппинг строки → RefType. Неизвестное имя → std::nullopt (вызывающая
// сторона обязана выдать диагностику, см. AGENTS п.5: без тихого fallback).
[[nodiscard]] inline std::optional<RefType> refTypeFromString(std::string_view s) noexcept {
#define TRUST_REF_TYPE_GEN_MAP(kind, name, value, cpp) {name, RefType::kind},
    static constexpr std::pair<std::string_view, RefType> kRefTypeMap[] = {TRUST_REF_TYPE_TYPES(TRUST_REF_TYPE_GEN_MAP)};
#undef TRUST_REF_TYPE_GEN_MAP
    for (const auto& [n, k] : kRefTypeMap) {
        if (s == n) {
            return k;
        }
    }
    return std::nullopt;
}

// ЕДИНЫЙ источник отображения СИМВОЛИЧЕСКОГО маркера ссылочного типа (позиция декларации/типа)
// на вид ссылки (RefType). Маркер - текст ref-оператора, который всегда начинается с `&`
// (`&&` → unique, `&*` → shared, `&?` → weak); вид ссылки задаётся У ТИПА (`x : &&Int32`) или
// у ПЕРЕМЕННОЙ при опущенном типе (`&& x := 5`). Одиночные `&`/`*` в позиции объявления НЕ
// используются (это операторы: `&` - захват ссылки, `*` - разыменование, см. SYNTAX.md).
// Мнемоника: `&&` (удвоение) = эксклюзивное владение («держу сам»), `&*` (`*` - «много») =
// совместное владение, `&?` (`?` - «может истечь») = слабый наблюдатель.
//   ВАЖНО: короткие маркеры допустимы ТОЛЬКО внутри модуля; на границе API (экспортируемые
//   имена) используется исключительно явный атрибут @[reftype("...")].
// Нативные (сырые) C++ виды `ptr`/`ref` (переходная ось совместимости) задаются ТОЛЬКО явным
// атрибутом `@[reftype("ptr"|"ref")]`; краткая символьная нотация `%&`/`%*` УДАЛЕНА. В выражении
// `&`/`*` - операторы (см. SYNTAX.md).
// Добавление вида ссылки, имеющего маркер, - одна строка в таблице TRUST_REF_TYPE_SIGILS.
#define TRUST_REF_TYPE_SIGILS(X) \
    X(kUnique, "&&")             \
    X(kShared, "&*")             \
    X(kWeak, "&?")
// Неизвестный маркер → std::nullopt (вызывающая сторона обязана выдать диагностику).
[[nodiscard]] inline std::optional<RefType> refTypeFromTypeSigil(std::string_view sigil) noexcept {
#define TRUST_REF_TYPE_SIGIL_GEN_MAP(kind, sigil_text) {sigil_text, RefType::kind},
    static constexpr std::pair<std::string_view, RefType> kRefTypeSigilMap[] = {TRUST_REF_TYPE_SIGILS(TRUST_REF_TYPE_SIGIL_GEN_MAP)};
#undef TRUST_REF_TYPE_SIGIL_GEN_MAP
    for (const auto& [s, k] : kRefTypeSigilMap) {
        if (sigil == s) {
            return k;
        }
    }
    return std::nullopt;
}

// -- Классификаторы вида ссылки (единый источник; заменяют инлайновые сравнения по месту) --
/// true для НАТИВНЫХ (сырых C++) видов ссылки: `ptr`/`ref`/`rref`/`ptrptr`.
[[nodiscard]] constexpr bool isNativeRefKind(RefType r) noexcept {
    return r == RefType::kPtr || r == RefType::kRef || r == RefType::kRref || r == RefType::kPtrPtr;
}

/// true для УМНЫХ (владеющих/охраняемых) видов ссылки: `shared`/`weak`/`unique`/`locker`.
[[nodiscard]] constexpr bool isSmartRefKind(RefType r) noexcept {
    return r == RefType::kShared || r == RefType::kWeak || r == RefType::kUnique || r == RefType::kLocker;
}

/// Совместимость ЗАЯВЛЕННОГО вида (атрибут/маркер) с ФАКТИЧЕСКИМ видом носителя:
/// носитель без вида (`kValue`) совместим с любым (вид будет применён); иначе виды обязаны совпадать.
[[nodiscard]] constexpr bool refKindCompatible(RefType declared, RefType actual) noexcept {
    return actual == RefType::kValue || actual == declared;
}

/// Виды ссылок, ПОЛНОСТЬЮ поддержанные семантикой и кодогенерацией. Зарезервированные, но
/// нереализованные виды (`rref`, `ptrptr`) отвергаются диагностикой (без тихого fallback):
/// достижимы только через `@[reftype(...)]`, но не имеют ни операций, ни lowering.
[[nodiscard]] constexpr bool isSupportedRefKind(RefType r) noexcept {
    return r != RefType::kRref && r != RefType::kPtrPtr;
}

// -- Ось ссылки (двухосевая модель; явная классификация вместо инлайновых сравнений) --
// RefType смешивает две независимые оси: ВЛАДЕНИЕ временем жизни (Value/Shared/Unique) и
// ДОСТУП к объекту (Native — сырой `&`/`*`; Locker — охраняемый). Ось — единая точка для
// проверок совместимости (value-vs-reference, копирование/move, swap).
enum class RefAxis : uint8_t {
    Value = 0,  ///< владение значением (без ссылки)
    Shared = 1, ///< совместное владение (shared-ось; weak строится из shared)
    Unique = 2, ///< эксклюзивное владение (unique-ось)
    Native = 3, ///< нативные (сырые) C++ ссылки/указатели (ptr/ref/rref/ptrptr)
    Locker = 4, ///< охраняемый доступ к reference-wrapper (результат lock())
};

[[nodiscard]] constexpr RefAxis refAxisOf(RefType r) noexcept {
    switch (r) {
    case RefType::kValue:
        return RefAxis::Value;
    case RefType::kShared:
    case RefType::kWeak:
        return RefAxis::Shared;
    case RefType::kUnique:
        return RefAxis::Unique;
    case RefType::kPtr:
    case RefType::kMptr:
    case RefType::kRef:
    case RefType::kRref:
    case RefType::kPtrPtr:
        return RefAxis::Native;
    case RefType::kLocker:
        return RefAxis::Locker;
    }
    return RefAxis::Value; // недостижимо: switch покрывает все значения RefType
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