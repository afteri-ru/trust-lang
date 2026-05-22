// attr_pool.cpp — AttrPool implementation

#include "ast/attr_pool.hpp"
#include "ast/token_info.hpp"

namespace trust {

// ────────────────────────────────────────────────────────────────────────────
// AttrPool constructor — zero-initializes builtin IDs, reserves slot 0
// ────────────────────────────────────────────────────────────────────────────

AttrPool::AttrPool()
: m_builtin_ids{}
, m_name_to_id() {
    // Reserve slot 0 as invalid AttrId (placeholder).
    // All real attributes start from ID 1.
    Attr placeholder;
    placeholder.m_id = 0;
    placeholder.m_name = std::string_view{};
    placeholder.m_builtin_kind = BuiltinAttrKind::kNone;
    m_attrs.push_back(std::move(placeholder));
}

// ────────────────────────────────────────────────────────────────────────────
// Attr::matches_params
// ────────────────────────────────────────────────────────────────────────────

bool Attr::matches_params(const std::vector<AttrParam>& params) const noexcept {
    if (!m_params.has_value() && params.empty())
        return true;
    if (!m_params.has_value() || m_params->size() != params.size())
        return false;

    for (std::size_t i = 0; i < m_params->size(); ++i) {
        const auto& stored = (*m_params)[i];
        const auto& given = params[i];
        // Compare by type first, then by value
        if (stored.m_value.index() != given.m_value.index())
            return false;
        if (stored.is_int() && stored.as_int() != given.as_int())
            return false;
        if (stored.is_string() && stored.as_string() != given.as_string())
            return false;
        if (stored.is_range()) {
            const auto& ra = stored.as_range();
            const auto& rb = given.as_range();
            if (ra.begin != rb.begin || ra.end != rb.end)
                return false;
        }
    }
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::register_attr (by param types)
// ────────────────────────────────────────────────────────────────────────────

AttrId AttrPool::register_attr(std::string_view name, std::vector<AttrParamType> required_param_types, BuiltinAttrKind kind) {
    EXPECT(kind == BuiltinAttrKind::kNone || (static_cast<std::size_t>(kind) > 0 && static_cast<std::size_t>(kind) < kBuiltinAttrCount));

    std::string_view interned_name = m_strings.intern(name);

    // Look up existing by name (O(1) via hash map)
    auto it = m_name_to_id.find(interned_name);
    if (it != m_name_to_id.end()) {
        AttrId id = it->second;
        auto idx = id & detail::kAttrIndexMask;
        EXPECT(idx < m_attrs.size());
        const auto& existing_req = m_attrs[idx].m_required_param_types;
        if (existing_req == required_param_types) {
            // If this is a built-in, ensure the kind matches
            if (kind != BuiltinAttrKind::kNone) {
                if (m_attrs[idx].m_builtin_kind == BuiltinAttrKind::kNone)
                    m_attrs[idx].m_builtin_kind = kind;
                else
                    EXPECT(m_attrs[idx].m_builtin_kind == kind);
            }
            return id;
        }
        // Name matches but param types differ — return existing (best effort)
        return id;
    }

    // Create new
    Attr attr;
    attr.m_id = static_cast<AttrId>(m_attrs.size());
    attr.m_name = interned_name;
    attr.m_params = std::nullopt;
    attr.m_required_param_types = std::move(required_param_types);
    if (kind != BuiltinAttrKind::kNone)
        attr.m_builtin_kind = kind;

    m_attrs.push_back(std::move(attr));
    AttrId new_id = m_attrs.back().m_id;
    m_name_to_id[interned_name] = new_id;

    // Record builtin ID if this is a built-in attribute
    if (kind != BuiltinAttrKind::kNone) {
        auto kind_idx = static_cast<std::size_t>(kind);
        if (m_builtin_ids[kind_idx] == 0)
            set_builtin_id(kind, new_id);
        else
            EXPECT(m_builtin_ids[kind_idx] == new_id);
    }

    return new_id;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::register_attr (by concrete params)
// ────────────────────────────────────────────────────────────────────────────

AttrId AttrPool::register_attr(std::string_view name, std::vector<AttrParam> params, BuiltinAttrKind kind) {
    EXPECT(kind == BuiltinAttrKind::kNone || (static_cast<std::size_t>(kind) > 0 && static_cast<std::size_t>(kind) < kBuiltinAttrCount));

    // Derive required param types from the concrete params
    std::vector<AttrParamType> required_types;
    required_types.reserve(params.size());
    for (const auto& p : params) {
        required_types.push_back(p.type());
    }

    std::string_view interned_name = m_strings.intern(name);

    // Intern string params
    for (auto& p : params) {
        if (p.is_string()) {
            p = AttrParam(m_strings.intern(p.as_string()));
        }
    }

    // Look up existing by name (O(1) via hash map)
    auto it = m_name_to_id.find(interned_name);
    if (it != m_name_to_id.end()) {
        AttrId id = it->second;
        auto idx = id & detail::kAttrIndexMask;
        EXPECT(idx < m_attrs.size());
        auto& existing = m_attrs[idx];
        if (existing.matches_params(params))
            return id;
        // Name matches but params differ — return existing (best effort)
        return id;
    }

    // Create new
    Attr attr;
    attr.m_id = static_cast<AttrId>(m_attrs.size());
    attr.m_name = interned_name;
    attr.m_params = std::move(params);
    attr.m_required_param_types = std::move(required_types);
    if (kind != BuiltinAttrKind::kNone)
        attr.m_builtin_kind = kind;

    m_attrs.push_back(std::move(attr));
    AttrId new_id = m_attrs.back().m_id;
    m_name_to_id[interned_name] = new_id;

    // Record builtin ID if this is a built-in attribute
    if (kind != BuiltinAttrKind::kNone) {
        auto kind_idx = static_cast<std::size_t>(kind);
        if (m_builtin_ids[kind_idx] == 0)
            set_builtin_id(kind, new_id);
        else
            EXPECT(m_builtin_ids[kind_idx] == new_id);
    }

    return new_id;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::lookup
// ────────────────────────────────────────────────────────────────────────────

std::optional<AttrId> AttrPool::lookup(std::string_view name) const noexcept {
    auto it = m_name_to_id.find(name);
    if (it != m_name_to_id.end())
        return it->second;
    return std::nullopt;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::has_attr (by name)
// ────────────────────────────────────────────────────────────────────────────

bool AttrPool::has_attr(std::string_view name) const noexcept {
    return m_name_to_id.find(name) != m_name_to_id.end();
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::has_attr (by BuiltinAttrKind)
// ────────────────────────────────────────────────────────────────────────────

bool AttrPool::has_attr(BuiltinAttrKind kind) const noexcept {
    auto idx = static_cast<std::size_t>(kind);
    if (idx == 0 || idx >= kBuiltinAttrCount)
        return false;
    return m_builtin_ids[idx] != 0;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::find_best_set_match — find set with maximum overlap
// ────────────────────────────────────────────────────────────────────────────

AttrPool::BestSetMatch AttrPool::find_best_set_match(const std::vector<AttrId>& sorted_ids) const {
    BestSetMatch result;

    for (const auto& set : m_sets) {
        // Count how many members of this set are in sorted_ids
        std::size_t overlap = 0;
        auto sit = set.m_members.begin();
        auto iit = sorted_ids.begin();
        while (sit != set.m_members.end() && iit != sorted_ids.end()) {
            if (*sit == *iit) {
                ++overlap;
                ++sit;
                ++iit;
            } else if (*sit < *iit) {
                ++sit;
            } else {
                ++iit;
            }
        }
        if (overlap > result.m_overlap) {
            result.m_id = set.m_id;
            result.m_overlap = overlap;
        }
    }

    return result;
}

// ────────────────────────────────────────────────────────────────────────────
// AttrPool::add_multi — batch attribute registration with optimal representation
// ────────────────────────────────────────────────────────────────────────────

std::vector<AttrId> AttrPool::add_multi(std::vector<AttrId> ids, bool create_set) {
    if (ids.empty())
        return {};

    // 1. Sort and deduplicate
    std::sort(ids.begin(), ids.end());
    auto last = std::unique(ids.begin(), ids.end());
    ids.erase(last, ids.end());

    // 2. Check for exact match in existing sets
    for (const auto& set : m_sets) {
        if (set.m_members == ids)
            return {set.m_id};
    }

    // 3. Find best matching set
    BestSetMatch best = find_best_set_match(ids);

    // 4. Compute missing IDs (those not in the best set)
    std::vector<AttrId> missing;
    if (best.m_id != 0) {
        const auto& best_set = get_set(best.m_id);
        auto it = ids.begin();
        auto sit = best_set.m_members.begin();
        while (it != ids.end()) {
            if (sit != best_set.m_members.end() && *it == *sit) {
                ++it;
                ++sit;
            } else if (sit != best_set.m_members.end() && *sit < *it) {
                ++sit;
            } else {
                missing.push_back(*it);
                ++it;
            }
        }
    } else {
        missing = ids;
    }

    // 5. Build result
    if (create_set) {
        // Combine best set + missing into a new set
        std::vector<AttrId> combined;
        if (best.m_id != 0) {
            const auto& best_set = get_set(best.m_id);
            combined = best_set.m_members;
        }
        combined.insert(combined.end(), missing.begin(), missing.end());
        std::sort(combined.begin(), combined.end());
        auto combined_last = std::unique(combined.begin(), combined.end());
        combined.erase(combined_last, combined.end());

        AttrId new_set_id = add_set(std::move(combined));
        return {new_set_id};
    } else {
        // Return best match + missing separately
        std::vector<AttrId> result;
        if (best.m_id != 0)
            result.push_back(best.m_id);
        result.insert(result.end(), missing.begin(), missing.end());
        return result;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// TokenInfo::add_attrs — batch attribute addition with optimal representation
// ────────────────────────────────────────────────────────────────────────────

void TokenInfo::add_attrs(AttrPool& pool, std::vector<AttrId> ids, bool create_set) {
    auto resolved = pool.add_multi(std::move(ids), create_set);
    for (auto id : resolved) {
        m_attrs.push_back(id);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// TokenInfo::has_attr (by BuiltinAttrKind) — O(n) on m_attrs
// ────────────────────────────────────────────────────────────────────────────

bool TokenInfo::has_attr(const AttrPool& pool, BuiltinAttrKind kind) const {
    AttrId target = pool.builtin_id(kind);
    // Check each attribute in this token, resolving sets to find matching singletons
    for (auto id : m_attrs) {
        auto resolved = pool.resolve(id);
        if (std::find(resolved.begin(), resolved.end(), target) != resolved.end())
            return true;
    }
    return false;
}

} // namespace trust
