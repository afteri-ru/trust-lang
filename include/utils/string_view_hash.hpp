// include/utils/string_view_hash.hpp
// Transparent hash for unordered containers keyed by std::string: lookup by std::string_view
// without creating a temporary std::string.
#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace trust {

struct StringViewHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
};

} // namespace trust
