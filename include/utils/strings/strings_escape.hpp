// include/utils/strings/strings_escape.hpp
#pragma once

#include "utils/error.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace trust::utils {

// Экранирует строку для встраивания в C++-строковый литерал: все специальные символы
// превращаются в C++-escape-последовательности. Прочие управляющие (< 0x20, 0x7F) - в hex-escape.
// Обратная операция - unescapeCppString. Единый источник для кодогенерации строковых литералов.
inline std::string escapeCppString(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\0':
            out += "\\0";
            break;
        case '\a':
            out += "\\a";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\v':
            out += "\\v";
            break;
        case '?':
            out += "\\?";
            break;
        default:
            if (c < 0x20 || c == 0x7F) {
                const char hex[] = "0123456789ABCDEF";
                out += "\\x";
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

// Обратная операция к escapeCppString: декодирует C++-escape-последовательности
// (backslash, quote, n, t, r, 0, a, b, f, v, ?, hex) обратно в символы.
inline std::string unescapeCppString(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c != '\\' || i + 1 >= s.size()) {
            out += c;
            continue;
        }
        const char e = s[++i];
        switch (e) {
        case '\\':
            out += '\\';
            break;
        case '"':
            out += '"';
            break;
        case 'n':
            out += '\n';
            break;
        case 't':
            out += '\t';
            break;
        case 'r':
            out += '\r';
            break;
        case '0':
            out += '\0';
            break;
        case 'a':
            out += '\a';
            break;
        case 'b':
            out += '\b';
            break;
        case 'f':
            out += '\f';
            break;
        case 'v':
            out += '\v';
            break;
        case '?':
            out += '?';
            break;
        case 'x': {
            int v = 0;
            int n = 0;
            while (n < 2 && i + 1 < s.size()) {
                const char h = s[i + 1];
                int d;
                if (h >= '0' && h <= '9') {
                    d = h - '0';
                } else if (h >= 'a' && h <= 'f') {
                    d = h - 'a' + 10;
                } else if (h >= 'A' && h <= 'F') {
                    d = h - 'A' + 10;
                } else {
                    break;
                }
                v = v * 16 + d;
                ++n;
                ++i;
            }
            out += static_cast<char>(v);
            break;
        }
        default:
            out += e;
        }
    }
    return out;
}

} // namespace trust::utils
