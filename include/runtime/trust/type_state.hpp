#pragma once

// Общее определение TypeId/QualType для компилятора (`types/type_id.hpp`) и рантайма
// (`stdlib/builtin/dict.hpp`, `trust/args.hpp`, `stdlib/builtin/range.hpp`): одно определение, один layout.
// Заголовок самодостаточен: рантайм-типы значений не зависят от дерева компилятора
// (utils/diag/registry). Компилятор подключает этот же файл как `runtime/trust/type_state.hpp`,
// поэтому include-путь компилятора не расширяется, а прочие переопределяемые runtime-заголовки
// остаются ему не видны.
// Одно 64-битное значение имеет ДВЕ интерпретации:
//   TypeId    — identity-value: TypeKind (upper 32) | registry_index (lower 24 bits);
//   QualType — qualifier-view ТОГО ЖЕ raw: identity + qualifier-байт (bits 24..31: const/inferred).
// Identity — БИТЫ БЕЗ qualifier-байта, поэтому operator==/operator<=>/std::hash<TypeId> сравнивают и
// хешируют ТОЛЬКО identity: qualified значение равно своей identity-проекции и не создаёт
// второй ключ в контейнере. raw() — полное сырое представление (ABI/диагностика), НЕ значение
// для сравнения типов.

#include <cassert>
#include <compare>
#include <concepts>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace trust {

/// Версия ABI-контракта рантайма (E14/R48): раскладка и семантика TypeId/QualType/TypeKind
/// и поставляемых рантайм-заголовков. Меняется только при несовместимом изменении контракта;
/// упаковщик фиксирует её в пакете, компилятор сверяет с ней.
inline constexpr std::uint32_t kRuntimeAbiVersion = 1;

/// TypeKind — упакованные характеристики типа (битовая структура — types/type/TYPE.md).
/// Единственный источник алиаса: `types/typekind.hpp` включает этот заголовок, а не объявляет
/// собственный `TypeKind` (иначе layout мог бы разойтись между компилятором и рантаймом).
using TypeKind = std::uint32_t;

// Рантайм-заголовки (dict/range/args) используют эти же значения; отдельного дублирования
// числовых кодов в runtime/trust/*.hpp нет.
enum class Group : std::uint8_t {
    kAuto = 0, // Sentinel «тип не выведен» / default решётки (Data=0, registry)
    kVoid = 1, // Void(Data=1), None(Data=2)

    kLogical,  // Bool(Data=1)
    kIntegers, // 8,16,32,64
    kUnsigned, // 8,16,32,64
    kNumbers,  // 16,32,64
    kBFloat,   // 16
    kComplex,  // 32,64
    // Произвольная точность: Rational (Data=1) и BigInteger (Data=2).
    kArbitraryPrecision,
    kStrChar, // 1
    kStrWide, // 1
    kDicts,   // 1

    kTensors,
    kContainers,
    kStructured,
    kCallable,
    kClasses,
    kRanges,
    kIterators,
    kDateTime,
    kAsync,
    kSync,
    kExceptions,
    kNative,
    kEllipsis,
    kArithmetics,
    kTemplateParam,
    kReftype,
    kEnums,
    kVariants,
    kNativeTemplate,
    kAccessPolicy,
    kNativeClass,
    kDeleterPolicy,
    kStructs,
    kClassDefs,
    kPackExpansion,
    kAny // Верхний тип (std::any), Data=0. Реализуется `builtin/any.src`, грузится ЯВНО.
};

constexpr std::uint32_t kTypeKindGroupShift = 0;
constexpr std::uint32_t kTypeKindGroupMask = 0x000000FFu;
constexpr std::uint32_t kTypeKindDataShift = 8;
constexpr std::uint32_t kTypeKindDataMask = 0x0000FF00u;
constexpr std::uint32_t kTypeKindRefTypeShift = 16;
constexpr std::uint32_t kTypeKindRefTypeMask = 0x000F0000u;
constexpr std::uint32_t kTypeKindClassShift = 20;
constexpr std::uint32_t kTypeKindClassMask = 0x00300000u;
constexpr std::uint32_t kTypeKindSizeUnitShift = 22;
constexpr std::uint32_t kTypeKindSizeUnitMask = 0x00400000u;
constexpr std::uint32_t kTypeKindBuiltinFlagShift = 23;
constexpr std::uint32_t kTypeKindBuiltinFlagMask = 0x00800000u;
constexpr std::uint32_t kTypeKindTrustFlagShift = 24;
constexpr std::uint32_t kTypeKindTrustFlagMask = 0x01000000u;
constexpr std::uint32_t kTypeKindAttrsFlagShift = 25;
constexpr std::uint32_t kTypeKindAttrsFlagMask = 0x02000000u;

/// Собрать TypeKind из Group + Data (базовая часть layout). Единственный способ кодирования
/// группы/данных для рантайма и тестов; компиляторный `makeTypeKind` добавляет остальные поля.
constexpr TypeKind makeGroupTypeKind(Group group, std::uint8_t data) noexcept {
    return static_cast<TypeKind>((static_cast<std::uint32_t>(group) << kTypeKindGroupShift) | (static_cast<std::uint32_t>(data) << kTypeKindDataShift));
}

/// registry_index занимает lower bits 0..23 (24 бита); байт 24–31 — qualifier-байт (const/inferred) QualType.
constexpr std::uint32_t kTypeIndexBits = 24;
constexpr std::uint32_t kTypeIndexMask = (1u << kTypeIndexBits) - 1;
constexpr std::uint32_t kMaxTypeIndex = kTypeIndexMask;

/// Флаг «тип выведен автоматически» (из литерала / inferred-переменной): ортогональный
/// квалификатор QualType, НЕ часть ключа интернирования. Интерпретация принадлежит QualType.
constexpr std::uint64_t kInferredFlag = 0x80000000ULL; // bit 31 нижней (registry_index) половины

/// Флаг «константность» type-level (`@[readonly]`, `:Array^`): квалификатор QualType,
/// identity-view (TypeId) его игнорирует.
constexpr std::uint64_t kConstFlag = 0x40000000ULL; // bit 30 нижней половины raw

/// Маска qualifier-байта (биты 24–31), снимаемая при переходе к identity (TypeId).
/// QualType хранит identity + const/inferred; flow-факты (DA) — в CFG/dataflow-проходе.
constexpr std::uint64_t kQualifierMask = 0xFF000000ULL;

static_assert(kTypeIndexBits == 24, "TypeId: registry_index must be exactly 24 bits");
static_assert((static_cast<std::uint64_t>(kMaxTypeIndex) & kQualifierMask) == 0, "TypeId: registry_index overlaps the qualifier byte");
static_assert((static_cast<std::uint64_t>(kMaxTypeIndex) | kQualifierMask) == 0xFFFFFFFFULL, "TypeId: lower half must consist of index + qualifier byte");
static_assert((kConstFlag & kInferredFlag) == 0, "TypeId: kConstFlag and kInferredFlag must be disjoint");
static_assert((kConstFlag & kQualifierMask) == kConstFlag, "TypeId: kConstFlag must live in the qualifier byte");
static_assert((kInferredFlag & kQualifierMask) == kInferredFlag, "TypeId: kInferredFlag must live in the qualifier byte");

// Класс с приватным raw-представлением: снаружи доступны fromRaw()/fromParts()/raw()/isValid()
// и сравнения. Это не даёт случайно смешать TypeId с uint64_t/индексом и централизует разбор бит.
class TypeId {
  public:
    constexpr TypeId() noexcept = default; // invalid

    /// Полное сырое представление (в т.ч. с qualifier-битами). Используется для ABI-границ
    /// (сгенерированный C++), диагностики и тестов; как ключ сравнения — только identity-проекция.
    static constexpr TypeId fromRaw(std::uint64_t raw) noexcept { return TypeId{raw}; }

    /// Identity-конструктор: kind + registry_index (qualifier-биты нулевые). Проверка диапазона —
    /// assert (в release вырезается); компилятор дополнительно валидирует вход через EXPECT.
    static constexpr TypeId fromParts(TypeKind kind, std::uint32_t registry_index) noexcept {
        assert(registry_index <= kMaxTypeIndex && "TypeId::fromParts: registry_index exceeds 24 bits");
        return TypeId{(static_cast<std::uint64_t>(kind) << 32) | registry_index};
    }
    /// Identity-конструктор без слота реестра (встроенные типы рантайма: kind уникален).
    static constexpr TypeId fromKind(TypeKind kind) noexcept { return fromParts(kind, 0); }

    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return m_raw; }

    /// Валидность определяется ТОЛЬКО identity-битами: qualifier-биты на неё не влияют
    /// (`fromRaw(kConstFlag)` остаётся invalid).
    [[nodiscard]] constexpr bool isValid() const noexcept { return (m_raw & ~kQualifierMask) != 0; }

    friend constexpr bool operator==(TypeId a, TypeId b) noexcept { return (a.m_raw & ~kQualifierMask) == (b.m_raw & ~kQualifierMask); }
    friend constexpr std::strong_ordering operator<=>(TypeId a, TypeId b) noexcept { return (a.m_raw & ~kQualifierMask) <=> (b.m_raw & ~kQualifierMask); }

  private:
    explicit constexpr TypeId(std::uint64_t raw) noexcept
    : m_raw(raw) {}
    std::uint64_t m_raw = 0;
};

inline constexpr TypeId INVALID_TYPE_ID{};

/// Identity-проекция: снимает ВСЕ qualifier-биты (сравнения, интернирование, emit).
constexpr TypeId getUnqualifiedType(TypeId id) noexcept {
    return TypeId::fromRaw(id.raw() & ~kQualifierMask);
}
/// Истина, если TypeId не несёт type-qualifier-битов (чистая identity).
constexpr bool isUnqualifiedType(TypeId id) noexcept {
    return (id.raw() & kQualifierMask) == 0;
}

constexpr TypeKind getTypeKind(TypeId id) noexcept {
    return static_cast<TypeKind>(id.raw() >> 32);
}

constexpr std::uint32_t getRegistryIndex(TypeId id) noexcept {
    // registry_index — identity-биты 0..23; qualifier-байт (24–31) в индекс не входит.
    return static_cast<std::uint32_t>(id.raw() & kTypeIndexMask);
}

/// Тип-состояние типизированного места/выражения (type-state).
/// qualifier-view того же raw: kind (32) | registry_index (24) | state (bits 24–31).
/// Это НЕ статический тип и НЕ ключ: identity — `getTypeId()` (снимает qualifier-байт).
/// НЕТ operator==/std::hash: для identity сравнивайте `getTypeId()`, для полного raw — `raw()`.
class QualType {
  public:
    constexpr QualType() noexcept = default; // invalid

    /// Конструктор из identity: TypeId по контракту не несёт квалификаторов, поэтому
    /// результат НЕквалифицирован. Квалификаторы задаются явно (`withConst`/`withInferred`).
    static constexpr QualType fromTypeId(TypeId id) noexcept { return QualType{getUnqualifiedType(id).raw()}; }
    /// Полное сырое представление (ABI-граница: сгенерированный C++ передаёт QualType целиком).
    static constexpr QualType fromRaw(std::uint64_t raw) noexcept { return QualType{raw}; }

    [[nodiscard]] constexpr TypeId getTypeId() const noexcept { return getUnqualifiedType(TypeId::fromRaw(m_raw)); }
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return m_raw; }

    /// Валидность identity; qualifier-биты на неё не влияют.
    [[nodiscard]] constexpr bool isValid() const noexcept { return getTypeId().isValid(); }

    [[nodiscard]] constexpr bool isConst() const noexcept { return (m_raw & kConstFlag) != 0; }
    [[nodiscard]] constexpr bool isInferred() const noexcept { return (m_raw & kInferredFlag) != 0; }

    [[nodiscard]] constexpr QualType withConst() const noexcept { return QualType{m_raw | kConstFlag}; }
    [[nodiscard]] constexpr QualType withInferred() const noexcept { return QualType{m_raw | kInferredFlag}; }
    [[nodiscard]] constexpr QualType withoutInferred() const noexcept { return QualType{m_raw & ~kInferredFlag}; }

    /// Заменить TypeKind, сохранив identity-индекс и qualifierы.
    [[nodiscard]] constexpr QualType withKind(TypeKind kind) const noexcept {
        return QualType{(static_cast<std::uint64_t>(kind) << 32) | static_cast<std::uint32_t>(m_raw)};
    }
    /// Заменить identity, сохранив qualifierы получателя.
    [[nodiscard]] constexpr QualType withIdentity(TypeId id) const noexcept { return QualType{getUnqualifiedType(id).raw() | (m_raw & kQualifierMask)}; }

  private:
    explicit constexpr QualType(std::uint64_t raw) noexcept
    : m_raw(raw) {}
    std::uint64_t m_raw = 0;
};

/// TypeKind состояния (симметрично `getTypeKind(TypeId)`); нужен рантайму для декодирования.
constexpr TypeKind kindOf(QualType state) noexcept {
    return getTypeKind(state.getTypeId());
}

} // namespace trust

static_assert(sizeof(trust::TypeId) == 8, "TypeId must stay an 8-byte value");
static_assert(alignof(trust::TypeId) == 8, "TypeId must stay 8-byte aligned");
static_assert(std::is_trivially_copyable_v<trust::TypeId>, "TypeId must be trivially copyable");
static_assert(std::is_standard_layout_v<trust::TypeId>, "TypeId must be standard layout");
static_assert(sizeof(trust::QualType) == 8, "QualType must stay an 8-byte value");
static_assert(alignof(trust::QualType) == 8, "QualType must stay 8-byte aligned");
static_assert(std::is_trivially_copyable_v<trust::QualType>, "QualType must be trivially copyable");
static_assert(std::is_standard_layout_v<trust::QualType>, "QualType must be standard layout");
// Контракт модели: state НЕ является identity-ключом, а два view не смешиваются неявно.
static_assert(!std::equality_comparable<trust::QualType>, "QualType must not be an equality key; compare getTypeId()");
static_assert(!std::is_convertible_v<trust::TypeId, trust::QualType>, "TypeId->QualType conversion must be explicit (QualType::fromTypeId)");
static_assert(!std::is_convertible_v<trust::QualType, trust::TypeId>, "QualType->TypeId conversion must be explicit (getTypeId())");

template <>
struct std::hash<trust::TypeId> {
    // Must match operator== (identity): qualifier-биты не входят в значение TypeId-ключа.
    size_t operator()(trust::TypeId id) const noexcept { return std::hash<std::uint64_t>{}(trust::getUnqualifiedType(id).raw()); }
};
