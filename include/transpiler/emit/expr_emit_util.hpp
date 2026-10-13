#pragma once
// include/transpiler/emit/expr_emit_util.hpp
// Внутренние строковые helper'ы эмиттеров выражений (общие для нескольких TU).

#include <string>
#include <string_view>

namespace trust {

/// Тело строкового литерала для C++-вывода: escape-последовательности Trust копируются
/// как есть, сырая `"` экранируется (актуально для StrWide, где ограничитель - `'`).
inline std::string escapeCppStringBody(std::string_view text) {
    std::string body;
    body.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size()) { // escape-последовательность - как есть
            body += c;
            body += text[++i];
        } else if (c == '"') {
            body += "\\\"";
        } else {
            body += c;
        }
    }
    return body;
}

} // namespace trust
