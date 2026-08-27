// attr_pool.hpp - AttrPool (mutable registry) for attributes
//
// AttrPool is the single point of registration for both built-in and
// user-defined attributes. There is no AttrType distinction - every
// attribute is simply a name + optional default parameter values.
//
// Built-in attributes are registered with register_builtin_attr (no source
// range); user-defined attributes use register_attr with the MapperRange of
// their definition site. The built-in bit in AttrId is derived from
// MapperRange validity: valid range = user-defined, invalid = built-in.
//
// Usage:
//   AttrPool pool;
//   AttrId id = pool.register_builtin_attr("readonly");
//   AttrId id2 = pool.register_attr("my_attr", {"param1", "param2"}, range);
//
// If an attribute with the same name already exists, the existing AttrId
// is returned (parameter values are validated on match).

#pragma once

#include "ast/attr.hpp"
#include <string_view>
#include <string>
#include <vector>
#include <cstddef>
#include <array>
#include <set>
#include <unordered_map>
#include <span>
#include <memory>

namespace trust {

// ----------------------------------------------------------------------------
// StringViewHash - transparent hash for std::string_view (to avoid copying)
// ----------------------------------------------------------------------------

struct StringViewHash {
    using is_transparent = void;
    [[nodiscard]] auto operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

// ----------------------------------------------------------------------------
// AttrPool - mutable attribute registry
// ----------------------------------------------------------------------------

class AttrPool {
  public:
    AttrPool();
    ~AttrPool() = default;

    /// Register all built-in attributes into this pool.
    /// Called automatically by constructor.
    static void registerBuiltinAttrs(AttrPool& pool);

    // Non-copyable, movable
    AttrPool(const AttrPool&) = delete;
    AttrPool& operator=(const AttrPool&) = delete;
    AttrPool(AttrPool&&) noexcept = default;
    AttrPool& operator=(AttrPool&&) noexcept = default;

    // -- Attribute registration --

    /// Register a user-defined attribute with given name and default parameter values.
    /// Parameters are stored as string_view values.
    /// def_range must be valid (the definition site in source).
    /// FAULT if an attribute with the same name already exists.
    AttrId register_attr(std::string_view name, std::vector<std::string_view> default_params, MapperRange def_range);

    /// Register a built-in attribute with given name and default parameter values.
    /// The def_range is invalid, so the built-in bit is set in the returned AttrId.
    /// FAULT if an attribute with the same name already exists.
    AttrId register_builtin_attr(std::string_view name, std::vector<std::string_view> default_params = {});

    // -- Queries --

    /// Look up an attribute by name, return AttrId or nullopt if not found (O(1)).
    [[nodiscard]] std::optional<AttrId> lookup(std::string_view name) const noexcept;

    /// Check if an attribute with the given name exists in the pool (O(1)).
    [[nodiscard]] bool has_attr(std::string_view name) const noexcept;

    /// Get the name of an attribute by its ID (O(1)).
    [[nodiscard]] std::string_view get_name(AttrId id) const;

    /// Get a singleton attribute by ID.
    [[nodiscard]] const Attr& get(AttrId id) const {
        auto idx = id & detail::kAttrIndexMask;
        EXPECT(idx < m_attrs.size());
        return m_attrs[idx];
    }

    /// Number of registered attributes
    [[nodiscard]] std::size_t attr_count() const noexcept { return m_attrs.size(); }

    /// Intern a string into the pool (stable set storage + dedup).
    [[nodiscard]] std::string_view intern(std::string_view s) {
        if (s.empty()) {
            return {};
        }
        // Use transparent comparator for heterogeneous lookup with string_view
        auto it = m_strings.find(s);
        if (it != m_strings.end()) {
            return std::string_view(*it);
        }
        return std::string_view(*m_strings.emplace(s).first);
    }

  private:
    /// Core registration shared by register_attr and register_builtin_attr.
    /// The built-in bit is derived from def_range.isInvalid().
    AttrId register_attr_impl(std::string_view name, std::vector<std::string_view> default_params, MapperRange def_range);

    std::vector<Attr> m_attrs;
    std::set<std::string, std::less<>> m_strings; // deduplicated string storage (stable references, hetero lookup)
    std::unordered_map<std::string_view, AttrId, StringViewHash, std::equal_to<>> m_name_to_id; // O(1) lookup by name (string_view into set)
};

} // namespace trust