#ifndef TYPES_CATEGORY_HPP
#define TYPES_CATEGORY_HPP

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace trust {

/*
 * ═══════════════════════════════════════════════════════════
 *  TypeKind Bit-Mask Architecture (64-bit)
 * ═══════════════════════════════════════════════════════════
 *
 *  Each TypeKind value encodes fields in a uint64_t:
 *
 *    ┌─────────────┬──────────┬────────┬────────┐
 *    │    Meta     │ Category │ Group  │ Index  │
 *    │  [63:48]    │ [47:40]  │[39:32] │ [31:0] │
 *    └─────────────┴──────────┴────────┴────────┘
 *
 *  Meta (16 bits, [63:48]):
 *    ┌──────┬──────┬──────────┬────────────┐
 *    │  UD  │  TC  │  Alias   │  Reserve   │
 *    │ [63] │ [62] │ [61:59]  │  [58:48]   │
 *    └──────┴──────┴──────────┴────────────┘
 *
 *  UserDefined (1 bit) — when set (1), indicates a user-defined type.
 *  TrivialCopy (1 bit) — when set (1), the type is trivially copyable.
 *  Alias (3 bits) — alias identifier within the category (values 0-7).
 *  Reserve (11 bits) — reserved for future use.
 *  Category (8 bits) — identifies the family of types.
 *  Group (8 bits) — distinguishes types within the same category.
 *  SizeIndex (32 bits) — index for actual bit-width.
 *
 *  ═══════════════════════════════════════════════════════════
 */

/* ── Fundamental type aliases ──────────────────────────── */
using type_kind_t = uint64_t;
using type_index_t = uint32_t;
using type_group_t = uint8_t;
using type_category_t = uint8_t;
using type_alias_t = uint8_t;

// ── Category type enum ───────────────────────────────
// Category object strored to std::variant and contain any object of subgroup
// Category object register buildin types in Types storage
// Category names are given in the plural to distinguish them from general types,
// since some of them have a name derived from the category name.
enum class Category : type_category_t {
    Void = 0,

    Integers,
    Numbers,
    Complex,
    Rationals,
    Strings,

    Structured, // Enum, Union
    Functions,  // Function, PureFunction
    Classes,    // Class, PureClass
    Methods,    // Method, PureMethod

    Templates,
    Tensors,

    // User = 255,
};

/* ── Group enums (formerly Subgroup) ─────────────────── */
enum class SubInteger : type_group_t { Default = 0, Ix = 1, Bool = 2 };
enum class SubFloat : type_group_t { Default = 0, Fx = 1 };
enum class SubComplex : type_group_t { Default = 0, Cx = 1 };
enum class SubString : type_group_t { Default = 0, Char = 1, Wide = 2 };
enum class SubTensor : type_group_t { Default = 0, Dense = 1, Sparse = 2 };
enum class SubContainer : type_group_t { Default = 0, Vector = 1, Map = 2, Deque = 3, Set = 4, MultiMap = 5, MultiSet = 6 };

/* ── Category → Group type mapping ───────────────────── */
template <Category Cat>
struct group_type_for;
template <>
struct group_type_for<Category::Integers> {
    using type = SubInteger;
};
template <>
struct group_type_for<Category::Numbers> {
    using type = SubFloat;
};
template <>
struct group_type_for<Category::Complex> {
    using type = SubComplex;
};
template <>
struct group_type_for<Category::Strings> {
    using type = SubString;
};
template <>
struct group_type_for<Category::Templates> {
    using type = SubContainer;
};
template <>
struct group_type_for<Category::Tensors> {
    using type = SubTensor;
};

template <Category Cat>
using group_type_for_t = typename group_type_for<Cat>::type;

/* ── Type meta: alias + flags + reserve ──────────────── */
struct type_meta_t {
    uint16_t value;

    static constexpr uint16_t kReserveLoWidth = 8;
    static constexpr uint16_t kReserveHiWidth = 3;
    static constexpr uint16_t kAliasWidth = 3;
    static constexpr uint16_t kTrivialCopyPos = 14;
    static constexpr uint16_t kUserDefinedPos = 15;

    constexpr type_meta_t()
    : value(0) {}
    constexpr type_meta_t(type_alias_t a, bool tc, bool ud)
    : value(static_cast<uint16_t>(static_cast<uint16_t>(a) << (kReserveLoWidth + kReserveHiWidth)) | static_cast<uint16_t>(tc ? 1u : 0u) << kTrivialCopyPos |
            static_cast<uint16_t>(ud ? 1u : 0u) << kUserDefinedPos) {}

    explicit constexpr type_meta_t(uint16_t v)
    : value(v) {}

    constexpr type_alias_t alias() const { return static_cast<type_alias_t>((value >> (kReserveLoWidth + kReserveHiWidth)) & 0x7); }
    constexpr bool trivial_copy() const { return (value >> kTrivialCopyPos) & 1; }
    constexpr bool user_defined() const { return (value >> kUserDefinedPos) & 1; }
};

/* ── TypeKind forward declaration ────────────────────── */
enum class TypeKind : type_kind_t;

/* ── KindOps: unified operations on TypeKind ─────────── */
struct KindOps {
    // ── Field widths (bits) ──
    static constexpr uint8_t kIndexWidth = 32;
    static constexpr uint8_t kGroupWidth = 8;
    static constexpr uint8_t kCategoryWidth = 8;
    static constexpr uint8_t kMetaWidth = 16;

    // ── Raw masks for single-field extraction (pre-meta) ──
    static constexpr type_kind_t kIndexMaskRaw = (static_cast<type_kind_t>(1) << kIndexWidth) - 1;
    static constexpr type_kind_t kGroupMaskRaw = (static_cast<type_kind_t>(1) << kGroupWidth) - 1;
    static constexpr type_kind_t kCategoryMaskRaw = (static_cast<type_kind_t>(1) << kCategoryWidth) - 1;

    // ── Computed shifts ──
    static constexpr uint8_t kIndexShift = 0;
    static constexpr uint8_t kGroupShift = kIndexShift + kIndexWidth;
    static constexpr uint8_t kCategoryShift = kGroupShift + kGroupWidth;
    static constexpr uint8_t kMetaShift = kCategoryShift + kCategoryWidth;

    // ── Computed masks ──
    static constexpr type_kind_t kIndexMask = kIndexMaskRaw << kIndexShift;
    static constexpr type_kind_t kGroupMask = kGroupMaskRaw << kGroupShift;
    static constexpr type_kind_t kCategoryMask = kCategoryMaskRaw << kCategoryShift;
    static constexpr type_kind_t kMetaMask = ((static_cast<type_kind_t>(1) << kMetaWidth) - 1) << kMetaShift;

    // ── Total width ──
    static constexpr uint8_t kTotalWidth = kMetaShift + kMetaWidth;
    static constexpr uint8_t kTotalBits = sizeof(type_kind_t) * 8;

    // ── Compile-time validation ──
    static_assert(kTotalWidth <= kTotalBits, "Total field width exceeds underlying type size");
    static_assert((kCategoryMask & kGroupMask) == 0, "Category/Group overlap");
    static_assert((kCategoryMask & kIndexMask) == 0, "Category/Index overlap");
    static_assert((kGroupMask & kIndexMask) == 0, "Group/Index overlap");
    static_assert((kMetaMask & kCategoryMask) == 0, "Meta/Category overlap");
    static_assert((kMetaMask & kGroupMask) == 0, "Meta/Group overlap");
    static_assert((kMetaMask & kIndexMask) == 0, "Meta/Index overlap");

    // ── Trivial copyable category check ──
    static constexpr bool is_trivially_copyable_cat(Category c) { return c == Category::Integers || c == Category::Numbers || c == Category::Strings; }

    // ── Construction ──
    template <typename Group>
    static constexpr type_kind_t make_kind_raw(Category category, Group group, type_index_t index) {
        type_kind_t tc_bit = is_trivially_copyable_cat(category) ? (static_cast<type_kind_t>(1) << (kMetaShift + type_meta_t::kTrivialCopyPos)) : 0;
        return tc_bit | (static_cast<type_kind_t>(category) << kCategoryShift) | (static_cast<type_kind_t>(group) << kGroupShift) |
               static_cast<type_kind_t>(index);
    }

    static constexpr type_kind_t make_kind_alias_static(type_kind_t base_raw, type_alias_t alias_val) {
        return (static_cast<type_kind_t>(alias_val) << (kMetaShift + type_meta_t::kReserveLoWidth + type_meta_t::kReserveHiWidth)) |
               (base_raw & (kIndexMask | kGroupMask | kCategoryMask | (static_cast<type_kind_t>(1) << (kMetaShift + type_meta_t::kTrivialCopyPos))));
    }

    template <typename Group>
    static constexpr TypeKind make_kind(Category category, Group group, type_index_t index) {
        return static_cast<TypeKind>(make_kind_raw(category, group, index));
    }

    // ── Decoding (raw) ──
    static constexpr type_kind_t raw(TypeKind k) { return static_cast<type_kind_t>(k); }

    // ── Decoding (runtime) — return typed values ──
    static constexpr type_alias_t alias_of(TypeKind k) {
        return static_cast<type_alias_t>((raw(k) >> (kMetaShift + type_meta_t::kReserveLoWidth + type_meta_t::kReserveHiWidth)) & 0x7);
    }
    static constexpr Category category_of(TypeKind k) { return static_cast<Category>((raw(k) >> kCategoryShift) & kCategoryMaskRaw); }
    static constexpr type_group_t group_of(TypeKind k) { return static_cast<type_group_t>((raw(k) >> kGroupShift) & kGroupMaskRaw); }
    static constexpr type_index_t index_of(TypeKind k) { return static_cast<type_index_t>(raw(k) & kIndexMaskRaw); }
    static constexpr type_meta_t meta_of(TypeKind k) { return type_meta_t{static_cast<uint16_t>((raw(k) >> kMetaShift) & 0xFFFF)}; }

    // ── Compile-time group extraction with auto-deduced group type ──
    template <TypeKind K>
    static constexpr auto group_of() {
        constexpr Category cat = category_of(K);
        using Group = typename group_type_for<cat>::type;
        return static_cast<Group>((raw(K) >> kGroupShift) & kGroupMaskRaw);
    }

    // ── Trivial copyable check ──
    template <TypeKind K>
    static constexpr bool is_trivially_copyable_v = (raw(K) & (static_cast<type_kind_t>(1) << (kMetaShift + type_meta_t::kTrivialCopyPos))) != 0;

    // ── Predicates ──
    static constexpr bool is_alias(TypeKind k) { return alias_of(k) != 0; }
    static constexpr bool is_arithmetic_cat(Category c) {
        return c == Category::Integers || c == Category::Numbers || c == Category::Complex || c == Category::Rationals;
    }

    // ── User-Defined Type operations ──
    static constexpr bool is_user_defined(TypeKind k) { return (raw(k) & (static_cast<type_kind_t>(1) << (kMetaShift + type_meta_t::kUserDefinedPos))) != 0; }
    static constexpr TypeKind make_user_defined(TypeKind base) {
        return static_cast<TypeKind>(raw(base) | (static_cast<type_kind_t>(1) << (kMetaShift + type_meta_t::kUserDefinedPos)));
    }
    static constexpr TypeKind strip_user_defined(TypeKind k) {
        return static_cast<TypeKind>(raw(k) & ~(static_cast<type_kind_t>(1) << (kMetaShift + type_meta_t::kUserDefinedPos)));
    }

    template <typename Group>
    static constexpr TypeKind make_user_kind(Category category, Group group, type_index_t index) {
        auto base = make_kind_raw(category, group, index);
        return static_cast<TypeKind>(base | (static_cast<type_kind_t>(1) << (kMetaShift + type_meta_t::kUserDefinedPos)));
    }

    template <TypeKind K>
    static constexpr bool is_user_defined_v = (raw(K) & (static_cast<type_kind_t>(1) << (kMetaShift + type_meta_t::kUserDefinedPos))) != 0;

    static constexpr Category get_base_category(TypeKind k) { return static_cast<Category>((raw(k) >> kCategoryShift) & kCategoryMaskRaw); }
};

/* ── Type classification: Category + Group ───────────── */
struct type_class_t {
    type_category_t category;
    type_group_t group;

    constexpr type_class_t()
    : category(0)
    , group(0) {}
    constexpr type_class_t(Category c, type_group_t g)
    : category(static_cast<type_category_t>(c))
    , group(g) {}
    constexpr type_class_t(type_category_t c, type_group_t g)
    : category(c)
    , group(g) {}

    constexpr bool operator==(type_class_t o) const { return category == o.category && group == o.group; }
    constexpr bool operator!=(type_class_t o) const { return !(*this == o); }
    constexpr bool operator<(type_class_t o) const { return category < o.category || (category == o.category && group < o.group); }

    static constexpr type_class_t from_kind(type_kind_t k) {
        return type_class_t(static_cast<type_category_t>((k >> KindOps::kCategoryShift) & KindOps::kCategoryMaskRaw),
                            static_cast<type_group_t>((k >> KindOps::kGroupShift) & KindOps::kGroupMaskRaw));
    }
};

} // namespace trust

#endif
