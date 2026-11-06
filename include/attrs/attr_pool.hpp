#pragma once

#include "attrs/attr.hpp"
#include <string_view>
#include <string>
#include <vector>
#include <cstddef>
#include <array>
#include <set>
#include <unordered_map>
#include <span>
#include <memory>

#include "utils/string_view_hash.hpp"

namespace trust {

class AttrPool {
  public:
    AttrPool();
    ~AttrPool() = default;

    /// Called automatically by constructor.
    static void registerBuiltinAttrs(AttrPool& pool);

    AttrPool(const AttrPool&) = delete;
    AttrPool& operator=(const AttrPool&) = delete;
    AttrPool(AttrPool&&) noexcept = default;
    AttrPool& operator=(AttrPool&&) noexcept = default;

    /// def_range must be valid (the definition site); FAULT on duplicate name.
    /// User-defined attributes are NOT processed by analyzer/codegen by default.
    AttrId register_attr(std::string_view name, std::vector<std::string_view> default_params, MapperRange def_range, bool analyzer_supported = false,
                         bool codegen_supported = false);

    /// def_range is invalid, so the built-in bit is set in the returned AttrId.
    /// analyzer_supported/codegen_supported mark which stage processes the attribute.
    /// FAULT on duplicate name.
    AttrId register_builtin_attr(std::string_view name, std::vector<std::string_view> default_params = {}, bool analyzer_supported = false,
                                 bool codegen_supported = false);

    [[nodiscard]] std::optional<AttrId> lookup(std::string_view name) const noexcept;

    [[nodiscard]] bool has_attr(std::string_view name) const noexcept;

    [[nodiscard]] std::string_view get_name(AttrId id) const;

    [[nodiscard]] const Attr& get(AttrId id) const {
        auto idx = id & detail::kAttrIndexMask;
        EXPECT(idx < m_attrs.size());
        return m_attrs[idx];
    }

    [[nodiscard]] std::size_t attr_count() const noexcept { return m_attrs.size(); }

    /// Stable storage (dedup): returned view is valid while the pool lives.
    [[nodiscard]] std::string_view intern(std::string_view s) {
        if (s.empty()) {
            return {};
        }
        auto it = m_strings.find(s);
        if (it != m_strings.end()) {
            return std::string_view(*it);
        }
        return std::string_view(*m_strings.emplace(s).first);
    }

  private:
    /// Built-in bit derived from def_range.isInvalid().
    AttrId register_attr_impl(std::string_view name, std::vector<std::string_view> default_params, MapperRange def_range, bool analyzer_supported,
                              bool codegen_supported);

    std::vector<Attr> m_attrs;
    std::set<std::string, std::less<>> m_strings;                                               // owned storage: stable addresses for string_views
    std::unordered_map<std::string_view, AttrId, StringViewHash, std::equal_to<>> m_name_to_id; // keys point into m_strings
};

} // namespace trust