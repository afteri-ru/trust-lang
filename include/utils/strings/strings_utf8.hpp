// include/utils/strings/strings_utf8.hpp
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
// -----------------------------------------------------------------------
//  UTF8: декодирование code point и таблица транслитерации
// -----------------------------------------------------------------------

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

/// Проверяет, является ли символ (Unicode code point) русской буквой.
inline bool isRussianLetter(uint32_t cp) noexcept {
    return (cp >= 0x0410 && cp <= 0x044F) || cp == 0x0401 || cp == 0x0451;
}

/// Проверяет, является ли символ (Unicode code point) Ъ или Ь (в любом регистре).
inline bool isHardSoftSign(uint32_t cp) noexcept {
    return cp == 0x042A || cp == 0x044A || cp == 0x042C || cp == 0x044C;
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

// Таблица транслитерации: русская буква → латинская строка
// Упорядочена по русскому алфавиту, заглавные и строчные
struct TranslitPair {
    uint32_t cp; // code point заглавной буквы
    const char* upper;
    const char* lower;
};

inline constexpr std::array<TranslitPair, 33> TRANSLIT_TABLE = {{
    {0x0410, "A", "a"},     // А
    {0x0411, "B", "b"},     // Б
    {0x0412, "V", "v"},     // В
    {0x0413, "G", "g"},     // Г
    {0x0414, "D", "d"},     // Д
    {0x0415, "E", "e"},     // Е
    {0x0401, "Yo", "yo"},   // Ё
    {0x0416, "Zh", "zh"},   // Ж
    {0x0417, "Z", "z"},     // З
    {0x0418, "I", "i"},     // И
    {0x0419, "Y", "y"},     // Й
    {0x041A, "K", "k"},     // К
    {0x041B, "L", "l"},     // Л
    {0x041C, "M", "m"},     // М
    {0x041D, "N", "n"},     // Н
    {0x041E, "O", "o"},     // О
    {0x041F, "P", "p"},     // П
    {0x0420, "R", "r"},     // Р
    {0x0421, "S", "s"},     // С
    {0x0422, "T", "t"},     // Т
    {0x0423, "U", "u"},     // У
    {0x0424, "F", "f"},     // Ф
    {0x0425, "Kh", "kh"},   // Х
    {0x0426, "Ts", "ts"},   // Ц
    {0x0427, "Ch", "ch"},   // Ч
    {0x0428, "Sh", "sh"},   // Ш
    {0x0429, "Sch", "sch"}, // Щ
    {0x042A, "", ""},       // Ъ (не передаётся)
    {0x042B, "Y", "y"},     // Ы
    {0x042C, "", ""},       // Ь (не передаётся)
    {0x042D, "E", "e"},     // Э
    {0x042E, "Yu", "yu"},   // Ю
    {0x042F, "Ya", "ya"},   // Я
}};

// Возвращает индекс в таблице транслитерации по code point, или -1
inline int translitIndex(uint32_t cp) noexcept {
    // Для строчных букв (а-я): cp ∈ [0x0430, 0x044F]
    // Сдвиг: заглавная на 0x20 меньше строчной для а-я
    uint32_t upper_cp = cp;
    if (cp >= 0x0430 && cp <= 0x044F) {
        upper_cp = cp - 0x20;
    } else if (cp == 0x0451) {
        upper_cp = 0x0401;
    }
    // теперь ищем
    for (int i = 0; i < 33; ++i) {
        if (TRANSLIT_TABLE[i].cp == upper_cp) {
            return i;
        }
    }
    return -1;
}

// Обратная таблица транслитерации: латинская строка → code point
// Для обратного преобразования нужен поиск по подстроке
// Будем использовать жадный поиск: пробуем сопоставить последовательность
// латинских букв с lower/upper полями таблицы

} // namespace detail

} // namespace trust::utils
