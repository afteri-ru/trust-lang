// attr.hpp — AST attribute system (base types, separate from pool and parser)
//
// Key Entities:
//   AttrId (uint32_t)    — attribute or set identifier.
//                           bit 31 = 1 for sets, 0 for singleton.
//                           bits 0-30 = index in AttrPool.
//   AttrParamType        — enum for parameter type validation (kInt, kString, kRange).
//   AttrParam            — a single parameter value (int64_t / string_view / MapperRange).
//   NamedAttrParam       — named parameter (name = value) for @attr(name = expr) syntax.
//   Attr                 — registered attribute descriptor with name, params, required_types.
//   AttrSet              — interned, immutable set of AttrId values.
//   detail::StringPool   — bump-allocated string interner (pool-local, not global).
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
// 3. Each attribute declares required types of its parameters at registration time.
//    The parser validates actual parameters against these types.
// 4. AttrId is a compact reference stored in TokenInfo.

#pragma once

#include "diag/location.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <set>
#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>

namespace trust {

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
    kCount ///< total number of built-in attribute kinds (sentinel, not a real kind)
};
// ────────────────────────────────────────────────────────────────────────────
// AttrParamType — type of an attribute parameter for validation
// ────────────────────────────────────────────────────────────────────────────

enum class AttrParamType : uint8_t {
    kInt = 0,
    kString = 1,
    kRange = 2,
};

// ────────────────────────────────────────────────────────────────────────────
// AttrParam — attribute parameter (value in @attr(...) brackets)
// ────────────────────────────────────────────────────────────────────────────

using AttrParamValue = std::variant<int64_t, std::string_view, MapperRange>;

struct AttrParam {
    AttrParamValue m_value;

    // Convenience constructors
    AttrParam() = default;
    explicit AttrParam(int64_t v)
    : m_value(v) {}
    explicit AttrParam(std::string_view v)
    : m_value(v) {}
    explicit AttrParam(MapperRange r)
    : m_value(r) {}

    [[nodiscard]] bool is_int() const noexcept { return std::holds_alternative<int64_t>(m_value); }
    [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<std::string_view>(m_value); }
    [[nodiscard]] bool is_range() const noexcept { return std::holds_alternative<MapperRange>(m_value); }

    [[nodiscard]] int64_t as_int() const { return std::get<int64_t>(m_value); }
    [[nodiscard]] std::string_view as_string() const { return std::get<std::string_view>(m_value); }
    [[nodiscard]] MapperRange as_range() const { return std::get<MapperRange>(m_value); }

    /// Return the AttrParamType for this value
    [[nodiscard]] AttrParamType type() const noexcept {
        if (is_int())
            return AttrParamType::kInt;
        if (is_string())
            return AttrParamType::kString;
        return AttrParamType::kRange;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// NamedAttrParam — named parameter (name = value) for @attr(name = expr)
// ────────────────────────────────────────────────────────────────────────────

/// Represents a named parameter in an attribute: name = value.
/// Used by the parser for @attr(name = expr) syntax.
struct NamedAttrParam {
    std::string_view m_name; ///< Parameter name (interned in pool)
    AttrParam m_value;       ///< Parameter value
};

// ────────────────────────────────────────────────────────────────────────────
// Attr — registered attribute (name + optional parameters + required param types)
// ────────────────────────────────────────────────────────────────────────────

struct Attr {
    AttrId m_id{0};
    BuiltinAttrKind m_builtin_kind{BuiltinAttrKind::kNone}; ///< Fast kind for built-in attrs, kNone for user-defined
    std::string_view m_name;                                // pointer into string pool
    std::optional<std::vector<AttrParam>> m_params;         // concrete parameter values
    std::vector<AttrParamType> m_required_param_types;      // types required at registration

    [[nodiscard]] bool has_params() const noexcept { return m_params.has_value(); }
    [[nodiscard]] std::size_t param_count() const noexcept { return m_params.has_value() ? m_params->size() : 0; }

    /// Check if the given parameter types match the required types
    [[nodiscard]] bool matches_params(const std::vector<AttrParam>& params) const noexcept;

    /// Human-readable string representation: "name" or "name(p1, p2, ...)"
    [[nodiscard]] std::string to_string() const;
};

// ────────────────────────────────────────────────────────────────────────────
// AttrSet — immutable set of attributes (internal detail, not part of public API)
// ────────────────────────────────────────────────────────────────────────────

struct AttrSet {
    AttrId m_id{0};
    std::vector<AttrId> m_members; // sorted for comparison

    // Comparator for std::set — by m_members content
    struct Compare {
        [[nodiscard]] bool operator()(const AttrSet& a, const AttrSet& b) const noexcept { return a.m_members < b.m_members; }
    };
};

// ────────────────────────────────────────────────────────────────────────────
// StringPool — bump allocator for strings
// ────────────────────────────────────────────────────────────────────────────

namespace detail {

class StringPool {
  public:
    StringPool() = default;
    ~StringPool() = default;

    // Non-copyable, movable
    StringPool(const StringPool&) = delete;
    StringPool& operator=(const StringPool&) = delete;
    StringPool(StringPool&&) noexcept = default;
    StringPool& operator=(StringPool&&) noexcept = default;

    /// Copy string into pool, return string_view (lives until pool destruction).
    /// Deduplicates: same string content returns the same pointer.
    [[nodiscard]] std::string_view intern(std::string_view s) {
        if (s.empty())
            return {};
        auto it = m_dedup.find(s);
        if (it != m_dedup.end())
            return it->first;
        auto* ptr = alloc(s.size());
        std::memcpy(ptr, s.data(), s.size());
        std::string_view stored{ptr, s.size()};
        m_dedup.emplace(stored, true);
        return stored;
    }

    /// Copy C-string into pool
    [[nodiscard]] std::string_view intern(const char* s) { return intern(std::string_view(s)); }

  private:
    static constexpr std::size_t kBlockSize = 4096;

    struct Block {
        std::unique_ptr<char[]> m_data;
        std::size_t m_used{0};
        std::size_t m_capacity;
        explicit Block(std::size_t cap)
        : m_data(std::make_unique<char[]>(cap))
        , m_capacity(cap) {}
    };

    std::vector<Block> m_blocks;
    Block* m_current{nullptr};
    std::unordered_map<std::string_view, bool> m_dedup;

    [[nodiscard]] char* alloc(std::size_t size) {
        if (!m_current || m_current->m_used + size > m_current->m_capacity) {
            auto cap = std::max(kBlockSize, size);
            m_blocks.emplace_back(cap);
            m_current = &m_blocks.back();
        }
        char* ptr = m_current->m_data.get() + m_current->m_used;
        m_current->m_used += size;
        return ptr;
    }
};

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