// attr_pool.hpp — AttrPool: unified registry for attributes
//
// AttrPool is the single point of registration for both built-in and
// user-defined attributes. There is no AttrType distinction — every
// attribute is simply a name + optional required parameter types.
//
// Usage:
//   AttrPool pool;
//   AttrId id = pool.register_attr("readonly");
//   AttrId id2 = pool.register_attr("my_attr", {AttrParamType::kInt, AttrParamType::kString});
//
// If an attribute with the same name already exists, the existing AttrId
// is returned (parameter types are validated on match).
//
// Note: AttrSet is an internal optimization and is not exposed in the public API.
// Use resolve() to flatten an AttrId to individual singletons.
// Use add_multi() for optimal batch attribute assignment to a terminal.

#pragma once

#include "ast/attr.hpp"
#include "diag/diag.hpp"
#include <string_view>
#include <vector>
#include <cstddef>
#include <array>
#include <unordered_map>

namespace trust {

// Number of built-in attribute kinds (including kNone)
inline constexpr std::size_t kBuiltinAttrCount = static_cast<std::size_t>(BuiltinAttrKind::kCount);

class AttrPool {
  public:
    AttrPool();
    ~AttrPool() = default;

    // Non-copyable, movable
    AttrPool(const AttrPool&) = delete;
    AttrPool& operator=(const AttrPool&) = delete;
    AttrPool(AttrPool&&) noexcept = default;
    AttrPool& operator=(AttrPool&&) noexcept = default;

    // ── Singleton attribute registration ──

    /// Register an attribute with given name and required parameter types.
    /// If an attribute with the same name and param types already exists,
    /// the existing AttrId is returned (no duplicate).
    /// On type mismatch a diagnostic error is issued.
    /// For built-in attributes, pass the corresponding BuiltinAttrKind (default kNone for user-defined).
    AttrId register_attr(std::string_view name, std::vector<AttrParamType> required_param_types = {}, BuiltinAttrKind kind = BuiltinAttrKind::kNone);

    /// Register an attribute with concrete parameter values.
    /// Required param types are derived from the given params.
    /// For built-in attributes, pass the corresponding BuiltinAttrKind (default kNone for user-defined).
    AttrId register_attr(std::string_view name, std::vector<AttrParam> params, BuiltinAttrKind kind = BuiltinAttrKind::kNone);

    // ── Batch attribute registration (optimal representation) ──

    /// Register a group of attributes together, finding the most compact representation.
    ///
    /// Steps:
    /// 1. Sort input IDs, remove duplicates.
    /// 2. Search for an existing set with exact match → return {set_id}.
    /// 3. Search for the set with maximum overlap with the input IDs.
    /// 4. If create_set is true: create a new set = best_matching + missing IDs, return {new_set_id}.
    /// 5. If create_set is false: return {best_matching_set_id, missing_id1, missing_id2, ...}.
    ///
    /// @return A minimal vector of AttrIds representing all input attributes.
    [[nodiscard]] std::vector<AttrId> add_multi(std::vector<AttrId> ids, bool create_set = true);

    // ── Queries ──

    /// Look up an attribute by name, return AttrId or nullopt if not found (O(1)).
    [[nodiscard]] std::optional<AttrId> lookup(std::string_view name) const noexcept;

    /// Check if an attribute with the given name exists in the pool (O(1)).
    [[nodiscard]] bool has_attr(std::string_view name) const noexcept;

    /// Check if a built-in attribute kind is registered (O(1)).
    [[nodiscard]] bool has_attr(BuiltinAttrKind kind) const noexcept;

    /// Get a singleton attribute by ID (FAULT if this is a set).
    [[nodiscard]] const Attr& get(AttrId id) const {
        EXPECT(!is_set(id));
        auto idx = id & detail::kAttrIndexMask;
        EXPECT(idx < m_attrs.size());
        return m_attrs[idx];
    }

    /// Resolve an AttrId to a flat vector of singleton attribute IDs.
    /// If id is a singleton, returns {id}.
    /// If id is a set, recursively collects all members, flattening nested sets.
    [[nodiscard]] std::vector<AttrId> resolve(AttrId id) const { return detail::resolve_attr_set(id, m_sets); }

    /// Number of registered singleton attributes
    [[nodiscard]] std::size_t attr_count() const noexcept { return m_attrs.size(); }

    /// Number of registered sets
    [[nodiscard]] std::size_t set_count() const noexcept { return m_sets.size(); }

    // ── Built-in attribute queries (O(1)) ──

    /// Get the AttrId for a built-in attribute kind (FAULT if kind is kNone or not registered)
    [[nodiscard]] AttrId builtin_id(BuiltinAttrKind kind) const {
        auto idx = static_cast<std::size_t>(kind);
        EXPECT(idx > 0 && idx < kBuiltinAttrCount);
        EXPECT(m_builtin_ids[idx] != 0);
        return m_builtin_ids[idx];
    }

    /// Check if an AttrId corresponds to a specific built-in kind (O(1))
    [[nodiscard]] bool is_builtin(AttrId id, BuiltinAttrKind kind) const { return !is_set(id) && id == builtin_id(kind); }

  private:
    /// Check if ID is a set (internal)
    [[nodiscard]] static bool is_set(AttrId id) noexcept { return (id & detail::kAttrSetFlag) != 0; }

    /// Get a set by ID (internal)
    [[nodiscard]] const AttrSet& get_set(AttrId id) const {
        EXPECT(is_set(id));
        auto idx = id & detail::kAttrIndexMask;
        EXPECT(idx < m_sets.size());
        return m_sets[idx];
    }

    /// Record the AttrId for a built-in kind (called during registration)
    void set_builtin_id(BuiltinAttrKind kind, AttrId id) {
        auto idx = static_cast<std::size_t>(kind);
        EXPECT(idx > 0 && idx < kBuiltinAttrCount);
        // Built-in ID slot must be assigned exactly once
        EXPECT(m_builtin_ids[idx] == 0);
        m_builtin_ids[idx] = id;
    }

    /// Create a set of attributes (or return existing one).
    /// Duplicate IDs in input are silently removed.
    AttrId add_set(std::vector<AttrId> ids) {
        // Sort for canonical representation
        std::sort(ids.begin(), ids.end());
        // Remove duplicates
        auto last = std::unique(ids.begin(), ids.end());
        ids.erase(last, ids.end());

        // Look up existing
        for (std::size_t i = 0; i < m_sets.size(); ++i) {
            if (m_sets[i].m_members == ids)
                return m_sets[i].m_id;
        }

        // Create new set
        AttrSet new_set;
        new_set.m_members = std::move(ids);
        new_set.m_id = detail::kAttrSetFlag | static_cast<AttrId>(m_sets.size());

        m_sets.push_back(std::move(new_set));
        return m_sets.back().m_id;
    }

    /// Find the set with maximum overlap with the given sorted unique IDs.
    /// Returns {best_set_id, overlap_count} or {0, 0} if no sets exist.
    struct BestSetMatch {
        AttrId m_id{0};
        std::size_t m_overlap{0};
    };
    [[nodiscard]] BestSetMatch find_best_set_match(const std::vector<AttrId>& sorted_ids) const;

    std::vector<Attr> m_attrs;
    std::vector<AttrSet> m_sets;
    detail::StringPool m_strings;
    std::array<AttrId, kBuiltinAttrCount> m_builtin_ids{};     // indexed by BuiltinAttrKind
    std::unordered_map<std::string_view, AttrId> m_name_to_id; // O(1) lookup by name
};

} // namespace trust