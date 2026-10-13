#include "sourcemap/store/output_buffer.hpp"

#include <string>
#include <string_view>

namespace trust {

// ══════════════════════════════════════════════════════════════
//                      OutputBuffer
// ══════════════════════════════════════════════════════════════

void OutputBuffer::prepend(std::string_view text, std::string_view ns) {
    std::string key(ns);
    m_prefixes[key].emplace(text);
}

void OutputBuffer::prependLeading(std::string_view text) {
    m_leading.append(text);
}

std::string OutputBuffer::build(unsigned indentSize) const {
    std::string result = m_leading;
    for (const auto& [ns, lines] : m_prefixes) {
        if (ns.empty()) {
            for (const auto& line : lines) {
                result.append(line);
                if (line.empty() || line.back() != '\n') {
                    result.push_back('\n');
                }
            }
        } else {
            result.append("namespace ");
            result.append(ns);
            result.append(" {\n");
            std::string indent(indentSize, ' ');
            for (const auto& line : lines) {
                result.append(indent);
                result.append(line);
                if (line.empty() || line.back() != '\n') {
                    result.push_back('\n');
                }
            }
            result.append("}\n");
        }
    }
    return result;
}

} // namespace trust
