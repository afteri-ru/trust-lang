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

// Wildcard-признак параметра атрибута (пустая строка = принимает любое значение).
inline bool attrParamIsWildcard(std::string_view d) noexcept {
    return d.empty();
}

using AttrId = uint32_t;

namespace detail {
static constexpr AttrId kAttrIndexMask = (1u << 24) - 1;
static constexpr AttrId kAttrBuiltinFlag = 1u << 24;
static constexpr AttrId kAttrAnalyzerFlag = 1u << 25;
static constexpr AttrId kAttrCodegenFlag = 1u << 26;
static constexpr AttrId kAttrManualFlag = 1u << 31;

[[nodiscard]] constexpr bool is_builtin(AttrId id) noexcept {
    return (id & kAttrBuiltinFlag) != 0;
}
[[nodiscard]] constexpr bool is_manual(AttrId id) noexcept {
    return (id & kAttrManualFlag) != 0;
}
[[nodiscard]] constexpr bool is_analyzer_supported(AttrId id) noexcept {
    return (id & kAttrAnalyzerFlag) != 0;
}
[[nodiscard]] constexpr bool is_codegen_supported(AttrId id) noexcept {
    return (id & kAttrCodegenFlag) != 0;
}

/// Оба флага (analyzer/codegen) сняты -> "unhandled" -> -Wunhandled-attr.
[[nodiscard]] constexpr bool is_handled(AttrId id) noexcept {
    return (id & (kAttrAnalyzerFlag | kAttrCodegenFlag)) != 0;
}

[[nodiscard]] constexpr AttrId with_builtin(AttrId id, bool builtin = true) noexcept {
    return builtin ? (id | kAttrBuiltinFlag) : (id & ~kAttrBuiltinFlag);
}

[[nodiscard]] constexpr AttrId with_manual(AttrId id, bool manual = true) noexcept {
    return manual ? (id | kAttrManualFlag) : (id & ~kAttrManualFlag);
}

[[nodiscard]] constexpr AttrId with_analyzer(AttrId id, bool analyzer = true) noexcept {
    return analyzer ? (id | kAttrAnalyzerFlag) : (id & ~kAttrAnalyzerFlag);
}

[[nodiscard]] constexpr AttrId with_codegen(AttrId id, bool codegen = true) noexcept {
    return codegen ? (id | kAttrCodegenFlag) : (id & ~kAttrCodegenFlag);
}
} // namespace detail

struct Attr {
    AttrId m_id{0};
    std::string_view m_name; // pointer into string pool
    std::vector<std::string_view> m_default_params;
    MapperRange m_def_range{}; // valid for user-defined, invalid for built-in

    [[nodiscard]] std::size_t param_count() const noexcept { return m_default_params.size(); }

    [[nodiscard]] bool has_params() const noexcept { return !m_default_params.empty(); }

    /// Пустой (wildcard) дефолт-параметр совпадает с ЛЮБЫМ строковым значением того же
    /// количества параметров (строковые атрибуты, напр. `link`, принимают произвольные значения).
    [[nodiscard]] bool matches_params(const std::vector<std::string_view>& params) const noexcept {
        const bool all_wildcard = std::all_of(m_default_params.begin(), m_default_params.end(), attrParamIsWildcard);
        if (all_wildcard) {
            // Аргументный атрибут (все дефолты - wildcard): параметры опциональны по количеству
            // (от 1 до числа дефолтов), напр. @[reftype("shared", <policy>, <timeout>)] - 1..3.
            if (m_default_params.empty()) {
                return params.empty();
            }
            return params.size() >= 1 && params.size() <= m_default_params.size();
        }
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