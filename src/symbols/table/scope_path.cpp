#include "symbols/scope_path.hpp"

namespace trust {

std::vector<std::string_view> namespaceSegmentsOf(std::string_view text) {
    std::vector<std::string_view> segments;
    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t separator = text.find("::", position);
        const std::string_view segment = separator == std::string_view::npos ? text.substr(position) : text.substr(position, separator - position);
        if (!segment.empty()) {
            segments.push_back(segment);
        }
        if (separator == std::string_view::npos) {
            break;
        }
        position = separator + 2;
    }
    return segments;
}

} // namespace trust
