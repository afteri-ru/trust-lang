#pragma once

// Слово по позиции в тексте файла — единственная реализация для reader/writer.
// Имя раскрытия (макроса) — проекция текста буфера (R1): отдельно не хранится.

#include <cstddef>
#include <string_view>

namespace trust {

namespace sourcemap_detail {
inline constexpr bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '@';
}
} // namespace sourcemap_detail

/// Слово, содержащее 1-based `offset`; пусто при невалидной позиции. `@` — часть слова (имя макроса).
[[nodiscard]] inline std::string_view wordAt(std::string_view text, uint32_t offset) {
    if (offset < 1 || offset > text.size()) {
        return {};
    }
    const std::size_t pos = offset - 1;
    if (!sourcemap_detail::isWordChar(text[pos])) {
        return {};
    }
    std::size_t start = pos;
    while (start > 0 && sourcemap_detail::isWordChar(text[start - 1])) {
        --start;
    }
    std::size_t end = pos;
    while (end < text.size() && sourcemap_detail::isWordChar(text[end])) {
        ++end;
    }
    return text.substr(start, end - start);
}

} // namespace trust