#pragma once

// JSON-экранирование строки: ЕДИНЫЙ источник для JSON-сериализаторов проекта
// (diag/serialize — диагностики, verify — сертификат `*.trustcert`). Внешние JSON-библиотеки
// не подключаются; байтовый порядок ключей — на совести вызывающего.

#include <string>
#include <string_view>

namespace trust::utils {

/// Экранирует строку для вставки в JSON-стр value: `"`, `\`, управляющие `\n`/`\r`/`\t`,
/// прочие символы < 0x20 — как `\u00XX` (строчные hex-цифры).
[[nodiscard]] inline std::string escapeJson(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                constexpr char kHex[] = "0123456789abcdef";
                out += "\\u00";
                out += kHex[(c >> 4) & 0xF];
                out += kHex[c & 0xF];
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

} // namespace trust::utils
