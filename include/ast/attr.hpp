// attr.hpp — AST attribute system (base types, separate from pool and parser)
//
// Key Entities:
//   AttrId (uint32_t)    — attribute or set identifier.
//                           bit 31 = 1 for sets, 0 for singleton.
//                           bits 0-30 = index in AttrPool.
//   AttrParamType        — enum for parameter type validation (kString, kRange).
//   AttrParam            — a single parameter value (string_view / MapperRange).
//   Attr                 — registered attribute descriptor with name and default params.
//   AttrSet              — interned, immutable set of AttrId values.
//
// Registration (AttrPool) is in attr_pool.hpp.
// Built-in attribute names (attr_names::kConst etc.) are in attr_builtin.hpp.
// Parser for @[...] syntax is in attr_parser.hpp.
// TokenInfo stores std::vector<AttrId> (see token_info.hpp).
//
// Design Principles:
// 1. There is no AttrType enum — all attributes, built-in or user-defined,
//    are stored uniformly and identified by their name string.
// 2. Attributes are unique by name. The same name always returns the same AttrId.
// 3. Each attribute stores its default parameter values in m_default_params.
//    Parameter types are derived from these values at registration time.
//    The parser validates actual parameters against these types.
// 4. AttrId is a compact reference stored in TokenInfo.
// 5. Parameters are always stored as string_view (raw text from source) or
//    MapperRange (range into source file, converted to string_view on demand).

#pragma once

#include "diag/location.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <algorithm>
#include <optional>

namespace trust {

// Forward declarations
template <typename FileIdx>
struct SourceMap;
using MapperFile = TaggedFile<MapperFileTag>;

// ────────────────────────────────────────────────────────────────────────────
// PackedName — compact {offset, length} packed into uint32_t
// ────────────────────────────────────────────────────────────────────────────

/// A packed pair of offset (24 bits, max ~16 MB) and length (8 bits, max 255)
/// into a name data buffer. The high 8 bits store length, the low 24 store offset.
struct PackedName {
    uint32_t m_value{0};

    static constexpr uint32_t kOffsetMask = 0x00FFFFFFu;
    static constexpr uint32_t kLengthShift = 24;

    PackedName() = default;

    explicit PackedName(uint32_t offset, uint32_t length) {
        EXPECT(length <= 255);
        EXPECT(offset <= kOffsetMask);
        m_value = (length << kLengthShift) | (offset & kOffsetMask);
    }

    [[nodiscard]] uint32_t offset() const noexcept { return m_value & kOffsetMask; }
    [[nodiscard]] uint32_t length() const noexcept { return m_value >> kLengthShift; }

    [[nodiscard]] bool operator==(const PackedName& other) const noexcept = default;
};

// ────────────────────────────────────────────────────────────────────────────
// AttrId — attribute or set identifier
// ────────────────────────────────────────────────────────────────────────────

using AttrId = uint32_t;

// Layout:
//   bit 31:   1 = set, 0 = singleton attr
//   bits 0-30: index in the corresponding pool
namespace detail {
static constexpr AttrId kAttrSetFlag = 1u << 31;
static constexpr AttrId kAttrIndexMask = (1u << 31) - 1;

/// First AttrId reserved for built-in attributes (built-in IDs start at 1).
/// User-defined attributes get IDs >= kFirstUserAttr.
static constexpr AttrId kFirstBuiltinId = 1;
} // namespace detail

// ────────────────────────────────────────────────────────────────────────────
// BuiltinAttrKind — enum for fast O(1) built-in attribute checks
// ────────────────────────────────────────────────────────────────────────────

/// Compact enum for all built-in attributes.
/// Values 0..kCount-1 map directly to AttrId range [1..kCount-1].
/// kNone = 0 means "user-defined or unknown".
enum class BuiltinAttrKind : uint8_t {
    kNone = 0,
    kConst,
    kPure,
    kSend,
    kSync,
    kThread,
    kReadOnly,
    kNoExcept,
    kStackGuard,
    kTrust,
    kRequire,
    kEnsure,
    kDependMacro,
    kCount ///< total number of built-in attribute kinds (sentinel, not a real kind)
};

// Number of built-in attribute kinds (including kNone)
inline constexpr std::size_t kBuiltinAttrCount = static_cast<std::size_t>(BuiltinAttrKind::kCount);

// ────────────────────────────────────────────────────────────────────────────
// AttrParamType — type of an attribute parameter for validation
// ────────────────────────────────────────────────────────────────────────────

enum class AttrParamType : uint8_t {
    kString = 0, // string_view
    kRange = 1,  // MapperRange (resolved to string_view via SourceMap on demand)
};

// ────────────────────────────────────────────────────────────────────────────
// AttrParam — attribute parameter (value in @attr(...) brackets)
// All parameters are stored either as string_view (raw text from source)
// or MapperRange (range into source file). Numeric parameters are NOT
// converted to int64_t — they remain as text from the source.
// ────────────────────────────────────────────────────────────────────────────

using AttrParamValue = std::variant<std::string_view, MapperRange>;

struct AttrParam {
    AttrParamValue m_value;

    // Convenience constructors
    AttrParam() = default;
    explicit AttrParam(std::string_view v)
    : m_value(v) {}
    explicit AttrParam(MapperRange r)
    : m_value(r) {}

    /// Check if two AttrParam are equal by string representation.
    [[nodiscard]] bool operator==(const AttrParam& other) const noexcept {
        if (m_value.index() != other.m_value.index())
            return false;
        if (is_string())
            return as_string() == other.as_string();
        // range: compare MapperRange directly
        const auto& ra = as_range();
        const auto& rb = other.as_range();
        return ra.begin == rb.begin && ra.end == rb.end;
    }

    [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<std::string_view>(m_value); }

    /// Return the raw string_view for kString params.
    /// FAULT if this is a kRange param.
    [[nodiscard]] std::string_view as_string() const {
        EXPECT(is_string());
        return std::get<std::string_view>(m_value);
    }

    /// Resolve parameter to string_view, resolving MapperRange via SourceMap.
    /// For kString: returns the stored string_view directly.
    /// For kRange: extracts text from source file using MapperRange begin/end offsets.
    [[nodiscard]] std::string_view as_string(const SourceMap<MapperFile>& mapper) const;

    /// Return the AttrParamType for this value (O(1) via variant index).
    [[nodiscard]] AttrParamType type() const noexcept {
        switch (m_value.index()) {
        case 0:
            return AttrParamType::kString;
        case 1:
            return AttrParamType::kRange;
        default:
            return AttrParamType::kString; // unreachable
        }
    }

  private:
    /// Internal access to MapperRange (for serialization, comparisons).
    [[nodiscard]] const MapperRange& as_range() const {
        EXPECT(!is_string());
        return std::get<MapperRange>(m_value);
    }
};

// ────────────────────────────────────────────────────────────────────────────
// Attr — registered attribute (name + optional default parameter values)
// ────────────────────────────────────────────────────────────────────────────

struct Attr {
    AttrId m_id{0};
    BuiltinAttrKind m_builtin_kind{BuiltinAttrKind::kNone}; ///< Fast kind for built-in attrs, kNone for user-defined
    std::string_view m_name;                                // pointer into string pool
    std::vector<AttrParam> m_default_params;                // default parameter values (empty = no params)
    bool m_variadic{false};                                 ///< If true, the last param type can repeat 0+ times

    /// Number of default parameters (== number of required param types).
    [[nodiscard]] std::size_t param_count() const noexcept { return m_default_params.size(); }

    /// Whether this attribute has any default parameters defined.
    [[nodiscard]] bool has_params() const noexcept { return !m_default_params.empty(); }

    /// Derive the required parameter types from m_default_params.
    [[nodiscard]] std::vector<AttrParamType> param_types() const {
        std::vector<AttrParamType> types;
        types.reserve(m_default_params.size());
        for (const auto& p : m_default_params) {
            types.push_back(p.type());
        }
        return types;
    }

    /// Check if the given parameter values match the default values (type + value).
    [[nodiscard]] bool matches_params(const std::vector<AttrParam>& params) const noexcept {
        if (m_default_params.size() != params.size())
            return false;
        for (std::size_t i = 0; i < m_default_params.size(); ++i) {
            if (!(m_default_params[i] == params[i]))
                return false;
        }
        return true;
    }

    /// Human-readable string representation: "name" or "name(p1, p2, ...)"
    [[nodiscard]] std::string to_string() const;

    /// Human-readable string representation with SourceMap for Range resolution.
    [[nodiscard]] std::string to_string(const SourceMap<MapperFile>& mapper) const;
};

// ────────────────────────────────────────────────────────────────────────────
// AttrSet — immutable set of attributes (internal detail, not part of public API)
// ────────────────────────────────────────────────────────────────────────────

struct AttrSet {
    AttrId m_id{0};
    std::vector<AttrId> m_members; // sorted for comparison
};

namespace detail {

/// Resolve an AttrId to a flat vector of singleton attribute IDs.
/// If id is a singleton, returns {id}.
/// If id is a set, recursively collects all members, flattening nested sets.
[[nodiscard]] inline std::vector<AttrId> resolve_attr_set(AttrId id, const std::vector<AttrSet>& sets) {
    if (!(id & kAttrSetFlag))
        return {id};

    auto idx = id & kAttrIndexMask;
    EXPECT(idx < sets.size());

    std::vector<AttrId> result;
    for (auto member : sets[idx].m_members) {
        if (member & kAttrSetFlag) {
            auto nested = resolve_attr_set(member, sets);
            result.insert(result.end(), nested.begin(), nested.end());
        } else {
            result.push_back(member);
        }
    }
    return result;
}

} // namespace detail

} // namespace trust