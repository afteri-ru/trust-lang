// attr.hpp - AST attribute system (base types, separate from pool and parser)
//
// Key Entities:
//   AttrId (uint32_t)    - attribute identifier with a bitmask:
//                           bits 0-29  = index in AttrPool.
//                           bit  30    = built-in (1) or user-defined (0).
//                           bit  31    = set manually (1) or automatically (0).
//   Attr                 - registered attribute descriptor with name and default params.
//
// Registration (AttrPool) is in attr_pool.hpp.
// Built-in attribute names (attr::ReadOnly etc.) are in attr_builtin.hpp.
// Parser for @[...] syntax is in attr_parser.hpp.
// AstNodeBase stores std::vector<AttrId> (see include/ast/ast_nodes.hpp).
//
// Design Principles:
// 1. There is no AttrType enum - all attributes, built-in or user-defined,
//    are stored uniformly and identified by their name string.
// 2. Attributes are unique by name. The same name always returns the same AttrId.
// 3. Each attribute stores its default parameter values in m_default_params.
//    Parameters are always stored as string_view (raw text from source).
// 4. AttrId is a compact reference stored in AstNodeBase.
// 5. The built-in flag is set at registration time; the manual flag is set
//    automatically when an attribute is attached to a node (add_attr with manual=true).

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <optional>
#include "utils/error.hpp"
#include "location/location.hpp"

namespace trust {

// ----------------------------------------------------------------------------
// AttrId - attribute identifier
// ----------------------------------------------------------------------------

using AttrId = uint32_t;

// Layout:
//   bit  30:   1 = built-in, 0 = user-defined
//   bit  31:   1 = set manually, 0 = set automatically
//   bits 0-29: index in AttrPool
namespace detail {
static constexpr AttrId kAttrBuiltinFlag = 1u << 30;
static constexpr AttrId kAttrManualFlag = 1u << 31;
static constexpr AttrId kAttrIndexMask = (1u << 30) - 1;

[[nodiscard]] constexpr bool is_builtin(AttrId id) noexcept {
    return (id & kAttrBuiltinFlag) != 0;
}
[[nodiscard]] constexpr bool is_manual(AttrId id) noexcept {
    return (id & kAttrManualFlag) != 0;
}

/// Return id with the built-in flag set (or cleared).
[[nodiscard]] constexpr AttrId with_builtin(AttrId id, bool builtin = true) noexcept {
    return builtin ? (id | kAttrBuiltinFlag) : (id & ~kAttrBuiltinFlag);
}

/// Return id with the manual flag set (or cleared).
[[nodiscard]] constexpr AttrId with_manual(AttrId id, bool manual = true) noexcept {
    return manual ? (id | kAttrManualFlag) : (id & ~kAttrManualFlag);
}
} // namespace detail

// ----------------------------------------------------------------------------
// Attr - registered attribute (name + optional default parameter values)
// ----------------------------------------------------------------------------

struct Attr {
    AttrId m_id{0};
    std::string_view m_name;                        // pointer into string pool
    std::vector<std::string_view> m_default_params; // default parameter values (empty = no params)
    MapperRange m_def_range{};                      // valid for user-defined, invalid for built-in

    /// Number of default parameters.
    [[nodiscard]] std::size_t param_count() const noexcept { return m_default_params.size(); }

    /// Whether this attribute has any default parameters defined.
    [[nodiscard]] bool has_params() const noexcept { return !m_default_params.empty(); }

    /// Check if the given parameter values match the default values.
    /// Пустой (wildcard) дефолт-параметр совпадает с ЛЮБЫМ предоставленным строковым
    /// значением того же количества параметров - так строковые атрибуты (напр. `link`)
    /// принимают произвольные значения (`@[link("m")]`), а не только фиксированные.
    /// Число параметров при этом должно совпадать.
    [[nodiscard]] bool matches_params(const std::vector<std::string_view>& params) const noexcept {
        if (m_default_params.size() != params.size()) {
            return false;
        }
        for (std::size_t i = 0; i < m_default_params.size(); ++i) {
            if (!(m_default_params[i].empty() || m_default_params[i] == params[i])) {
                return false;
            }
        }
        return true;
    }

    /// Human-readable string representation: "name" or "name(p1, p2, ...)"
    [[nodiscard]] std::string to_string() const;
};

} // namespace trust