#pragma once

// include/sourcemap/store/output_buffer.hpp
// OutputBuffer: буфер prepend-данных выходного C++ файла.

#include <map>
#include <set>
#include <string>
#include <string_view>

namespace trust {

// ══════════════════════════════════════════════════════════════
//  OutputBuffer: буфер prepend-данных выходного C++ файла
// ══════════════════════════════════════════════════════════════
struct OutputBuffer {
    // ns -> set of unique prefix strings
    std::map<std::string, std::set<std::string>> m_prefixes;
    // Непорядковый «leading» префикс, эмитится ПЕРВЫМ (до отсортированного множества m_prefixes).
    // Используется для шапки автогенерируемого файла/лицензии, чтобы они были первой строкой.
    std::string m_leading;

    void prepend(std::string_view text, std::string_view ns = "");
    void prependLeading(std::string_view text);
    std::string build(unsigned indentSize = 4) const;
};

} // namespace trust
