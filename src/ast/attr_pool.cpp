// attr_pool.cpp — AttrPool implementation

#include "ast/attr_pool.hpp"
#include "ast/attr_builtin.hpp"

#include <cstring>
#include <array>

namespace trust {

// ────────────────────────────────────────────────────────────────────────────
// AttrPool constructor — reserves slot 0
// ────────────────────────────────────────────────────────────────────────────

AttrPool::AttrPool()
: m_name_to_id() {
    // Reserve slot 0 as invalid AttrId (placeholder).
    // All real attributes start from ID 1.
    Attr placeholder;
    placeholder.m_id = 0;
    placeholder.m_name = std::string_view{};
    m_attrs.push_back(std::move(placeholder));

    // Register all built-in attributes
    registerBuiltinAttrs(*this);
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::register_attr_impl — shared registration (FAULT on duplicate name)
// ────────────────────────────────────────────────────────────────────────────

AttrId AttrPool::register_attr_impl(std::string_view name, std::vector<std::string_view> default_params, MapperRange def_range) {
    std::string_view interned_name = intern(name);

    // Intern string params
    for (auto& p : default_params) {
        p = intern(p);
    }

    // FAULT if duplicate name (use transparent hash — no std::string copy)
    if (m_name_to_id.find(interned_name) != m_name_to_id.end()) {
        FAULT("AttrPool::register_attr: attribute '{}' already registered", interned_name);
    }

    // Create new attribute
    // The built-in bit is derived from the validity of the definition range:
    // built-in attributes have no source range (invalid), user-defined have a valid one.
    bool builtin = def_range.isInvalid();
    Attr attr;
    attr.m_id = detail::with_builtin(static_cast<AttrId>(m_attrs.size()), builtin);
    attr.m_name = interned_name;
    attr.m_default_params = std::move(default_params);
    attr.m_def_range = def_range;

    m_attrs.push_back(std::move(attr));
    AttrId new_id = m_attrs.back().m_id;
    // interned_name is already a stable string_view into m_strings, safe to use as key
    m_name_to_id[interned_name] = new_id;

    return new_id;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::register_attr — user-defined attribute
// ────────────────────────────────────────────────────────────────────────────

AttrId AttrPool::register_attr(std::string_view name, std::vector<std::string_view> default_params, MapperRange def_range) {
    return register_attr_impl(name, std::move(default_params), def_range);
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::register_builtin_attr — built-in attribute
// ────────────────────────────────────────────────────────────────────────────

AttrId AttrPool::register_builtin_attr(std::string_view name, std::vector<std::string_view> default_params) {
    return register_attr_impl(name, std::move(default_params), MapperRange{});
}

// ────────────────────────────────────────────────────────────────────────────
// Attr::to_string — human-readable representation
// ────────────────────────────────────────────────────────────────────────────

std::string Attr::to_string() const {
    std::string result(m_name);

    if (!m_default_params.empty()) {
        result += "(";
        for (std::size_t i = 0; i < m_default_params.size(); ++i) {
            if (i > 0) {
                result += ", ";
            }
            result += "\"";
            result += m_default_params[i];
            result += "\"";
        }
        result += ")";
    }

    return result;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::lookup (transparent hash — no std::string copy)
// ────────────────────────────────────────────────────────────────────────────

std::optional<AttrId> AttrPool::lookup(std::string_view name) const noexcept {
    auto it = m_name_to_id.find(name);
    if (it != m_name_to_id.end()) {
        return it->second;
    }
    return std::nullopt;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::get_name
// ────────────────────────────────────────────────────────────────────────────

std::string_view AttrPool::get_name(AttrId id) const {
    auto idx = id & detail::kAttrIndexMask;
    EXPECT(idx < m_attrs.size());
    return m_attrs[idx].m_name;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::has_attr (by name, transparent hash)
// ────────────────────────────────────────────────────────────────────────────

bool AttrPool::has_attr(std::string_view name) const noexcept {
    return m_name_to_id.find(name) != m_name_to_id.end();
}

} // namespace trust