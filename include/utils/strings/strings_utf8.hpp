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

namespace detail {

/// Возвращает длину UTF8 последовательности по ведущему байту.
inline int utf8SeqLen(uint8_t lead) noexcept {
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 1; // невалидный lead, считаем 1 байтом
}

/// Проверяет, является ли байт continuation-байтом UTF8 (10xxxxxx).
inline bool isUtf8Cont(uint8_t b) noexcept {
    return (b & 0xC0) == 0x80;
}

/// Декодирует один UTF8 символ из s[offset], возвращает code point
/// и продвигает offset на длину последовательности.
/// Если символ некорректен, возвращает 0xFFFFFFFF и offset не меняется.
inline uint32_t decode_utf8_at(std::string_view s, size_t& offset) {
    if (offset >= s.size()) {
        return 0xFFFFFFFF;
    }
    uint8_t b0 = static_cast<uint8_t>(s[offset]);
    if (b0 < 0x80) {
        uint32_t cp = b0;
        offset += 1;
        return cp;
    }
    int seq_len = utf8SeqLen(b0);
    if (seq_len < 2 || offset + seq_len > s.size()) {
        // невалидный UTF8 - пропускаем один байт
        offset += 1;
        return 0xFFFFFFFF;
    }
    uint32_t cp;
    switch (seq_len) {
    case 2:
        cp = (static_cast<uint32_t>(b0 & 0x1F) << 6) | static_cast<uint32_t>(s[offset + 1] & 0x3F);
        break;
    case 3:
        cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) | (static_cast<uint32_t>(s[offset + 1] & 0x3F) << 6) | static_cast<uint32_t>(s[offset + 2] & 0x3F);
        break;
    case 4:
        cp = (static_cast<uint32_t>(b0 & 0x07) << 18) | (static_cast<uint32_t>(s[offset + 1] & 0x3F) << 12) |
             (static_cast<uint32_t>(s[offset + 2] & 0x3F) << 6) | static_cast<uint32_t>(s[offset + 3] & 0x3F);
        break;
    default:
        offset += 1;
        return 0xFFFFFFFF;
    }
    offset += seq_len;
    return cp;
}

} // namespace detail

} // namespace trust::utils
