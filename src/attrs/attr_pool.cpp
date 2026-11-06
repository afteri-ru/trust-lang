#include "attrs/attr_pool.hpp"
#include "attrs/attr_builtin.hpp"

#include <cstring>
#include <array>

namespace trust {

AttrPool::AttrPool()
: m_name_to_id() {
    // Slot 0 = invalid AttrId (placeholder); real attributes start from ID 1.
    Attr placeholder;
    placeholder.m_id = 0;
    placeholder.m_name = std::string_view{};
    m_attrs.push_back(std::move(placeholder));

    registerBuiltinAttrs(*this);
}

AttrId AttrPool::register_attr_impl(std::string_view name, std::vector<std::string_view> default_params, MapperRange def_range, bool analyzer_supported,
                                    bool codegen_supported) {
    std::string_view interned_name = intern(name);

    for (auto& p : default_params) {
        p = intern(p);
    }

    // FAULT on duplicate name (transparent hash: no std::string copy).
    if (m_name_to_id.find(interned_name) != m_name_to_id.end()) {
        FAULT("AttrPool::register_attr: attribute '{}' already registered", interned_name);
    }

    // builtin: no source range (invalid) = built-in; valid range = user-defined.
    bool builtin = def_range.isInvalid();
    Attr attr;
    attr.m_id = detail::with_builtin(static_cast<AttrId>(m_attrs.size()), builtin);
    attr.m_id = detail::with_analyzer(attr.m_id, analyzer_supported);
    attr.m_id = detail::with_codegen(attr.m_id, codegen_supported);
    attr.m_name = interned_name;
    attr.m_default_params = std::move(default_params);
    attr.m_def_range = def_range;

    m_attrs.push_back(std::move(attr));
    AttrId new_id = m_attrs.back().m_id;
    // interned_name is already a stable string_view into m_strings, safe to use as key
    m_name_to_id[interned_name] = new_id;

    return new_id;
}

AttrId AttrPool::register_attr(std::string_view name, std::vector<std::string_view> default_params, MapperRange def_range, bool analyzer_supported,
                               bool codegen_supported) {
    return register_attr_impl(name, std::move(default_params), def_range, analyzer_supported, codegen_supported);
}

AttrId AttrPool::register_builtin_attr(std::string_view name, std::vector<std::string_view> default_params, bool analyzer_supported, bool codegen_supported) {
    return register_attr_impl(name, std::move(default_params), MapperRange{}, analyzer_supported, codegen_supported);
}

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

std::optional<AttrId> AttrPool::lookup(std::string_view name) const noexcept {
    auto it = m_name_to_id.find(name);
    if (it != m_name_to_id.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string_view AttrPool::get_name(AttrId id) const {
    auto idx = id & detail::kAttrIndexMask;
    EXPECT(idx < m_attrs.size());
    return m_attrs[idx].m_name;
}

bool AttrPool::has_attr(std::string_view name) const noexcept {
    return m_name_to_id.find(name) != m_name_to_id.end();
}

} // namespace trust