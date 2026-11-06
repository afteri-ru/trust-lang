#pragma once
// UTF-8 <-> широкие символы — РАНТАЙМ-ПРИМИТИВ (не реализация trust-типа).
//
// Классы строк (trust::StrChar/trust::StrWide) ГЕНЕРИРУЮТСЯ из stdlib/builtin/string.src;
// их методы toWide()/toNarrow() зовут эти функции. Алгоритм живёт здесь как примитив по той же
// причине, что assert/format: он не выражается типами TrustLang (побайтовый разбор кодовых точек),
// а держать его EMBED-текстом в каждом .src значило бы дублировать.
//
// Контракт: некорректный байт UTF-8 -> U+FFFD (символ замены), без исключений.

#include <cstddef>
#include <cstdint>
#include <string>

namespace trust::utf8 {

/// UTF-8 (std::string) -> широкие символы.
[[nodiscard]] inline std::wstring decode(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c0 = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (c0 < 0x80) {
            cp = c0;
        } else if ((c0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = (static_cast<std::uint32_t>(c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            len = 2;
        } else if ((c0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = (static_cast<std::uint32_t>(c0 & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3F);
            len = 3;
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = (static_cast<std::uint32_t>(c0 & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
            len = 4;
        }
        out.push_back(static_cast<wchar_t>(cp));
        i += len;
    }
    return out;
}

/// Широкие символы -> UTF-8 (std::string).
[[nodiscard]] inline std::string encode(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (const wchar_t wc : w) {
        const std::uint32_t cp = static_cast<std::uint32_t>(wc);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

} // namespace trust::utf8