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
//  Вспомогательные утилиты для работы с UTF8
// -----------------------------------------------------------------------

namespace detail {

/// Возвращает длину UTF8 последовательности по ведущему байту.
inline int utf8_seq_len(uint8_t lead) noexcept {
    if (lead < 0x80)
        return 1;
    if ((lead & 0xE0) == 0xC0)
        return 2;
    if ((lead & 0xF0) == 0xE0)
        return 3;
    if ((lead & 0xF8) == 0xF0)
        return 4;
    return 1; // невалидный lead, считаем 1 байтом
}

/// Проверяет, является ли байт continuation-байтом UTF8 (10xxxxxx).
inline bool is_utf8_cont(uint8_t b) noexcept {
    return (b & 0xC0) == 0x80;
}

/// Проверяет, является ли символ (Unicode code point) русской буквой.
inline bool is_russian_letter(uint32_t cp) noexcept {
    return (cp >= 0x0410 && cp <= 0x044F) || cp == 0x0401 || cp == 0x0451;
}

/// Проверяет, является ли символ (Unicode code point) Ъ или Ь (в любом регистре).
inline bool is_hard_soft_sign(uint32_t cp) noexcept {
    return cp == 0x042A || cp == 0x044A || cp == 0x042C || cp == 0x044C;
}

/// Декодирует один UTF8 символ из s[offset], возвращает code point
/// и продвигает offset на длину последовательности.
/// Если символ некорректен, возвращает 0xFFFFFFFF и offset не меняется.
inline uint32_t decode_utf8_at(std::string_view s, size_t& offset) {
    if (offset >= s.size())
        return 0xFFFFFFFF;
    uint8_t b0 = static_cast<uint8_t>(s[offset]);
    if (b0 < 0x80) {
        uint32_t cp = b0;
        offset += 1;
        return cp;
    }
    int seq_len = utf8_seq_len(b0);
    if (seq_len < 2 || offset + seq_len > s.size()) {
        // невалидный UTF8 — пропускаем один байт
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
inline int translit_index(uint32_t cp) noexcept {
    // Для строчных букв (а-я): cp ∈ [0x0430, 0x044F]
    // Сдвиг: заглавная на 0x20 меньше строчной для а-я
    uint32_t upper_cp = cp;
    if (cp >= 0x0430 && cp <= 0x044F)
        upper_cp = cp - 0x20;
    else if (cp == 0x0451)
        upper_cp = 0x0401;
    // теперь ищем
    for (int i = 0; i < 33; ++i)
        if (TRANSLIT_TABLE[i].cp == upper_cp)
            return i;
    return -1;
}

// Обратная таблица транслитерации: латинская строка → code point
// Для обратного преобразования нужен поиск по подстроке
// Будем использовать жадный поиск: пробуем сопоставить последовательность
// латинских букв с lower/upper полями таблицы

} // namespace detail

// -----------------------------------------------------------------------
//  Базовые строковые операции
// -----------------------------------------------------------------------

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

inline bool is_number(std::string_view s) {
    if (s.empty())
        return false;
    size_t i = 0;
    if (s[0] == '-') {
        i = 1;
        if (i >= s.size())
            return false; // только минус — не число
    }
    for (; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
    return true;
}

inline std::vector<std::string_view> tokenize(std::string_view s) {
    std::vector<std::string_view> tokens;
    const char* ptr = s.data();
    size_t len = s.size();
    size_t i = 0;
    while (i < len) {
        while (i < len && (ptr[i] == ' ' || ptr[i] == '\t'))
            ++i;
        if (i >= len)
            break;
        size_t start = i;
        while (i < len && ptr[i] != ' ' && ptr[i] != '\t')
            ++i;
        tokens.push_back(std::string_view(ptr + start, i - start));
    }
    return tokens;
}

// -----------------------------------------------------------------------
//  Работа с идентификаторами
// -----------------------------------------------------------------------

inline std::string_view extract_name(std::string_view s, size_t offset) {
    if (s.empty() || offset >= s.size())
        return {};

    // Если offset указывает на continuation-байт UTF8, откатываемся
    // к началу последовательности
    while (offset > 0 && detail::is_utf8_cont(static_cast<uint8_t>(s[offset])))
        --offset;

    // Проверяет, может ли символ быть первым в идентификаторе
    auto is_ident_start = [](uint32_t cp) -> bool {
        if (cp < 0x80)
            return std::isalpha(static_cast<int>(cp)) || cp == '_' || cp == ':';
        return true; // UTF8 буквы
    };
    // Проверяет, может ли символ быть внутри идентификатора (первые и последующие)
    auto is_ident_cont = [](uint32_t cp) -> bool {
        if (cp < 0x80)
            return std::isalnum(static_cast<int>(cp)) || cp == '_' || cp == ':';
        return true;
    };

    size_t pos = offset;
    uint32_t cp;
    size_t tmp = pos;
    cp = detail::decode_utf8_at(s, tmp);
    if (cp == 0xFFFFFFFF)
        return {};

    // Если символ не может быть началом, но может быть продолжением — ищем начало слева
    if (!is_ident_start(cp)) {
        if (!is_ident_cont(cp))
            return {};
        // Ищем начало идентификатора слева
        size_t search_pos = offset;
        bool found_start = false;
        while (search_pos > 0) {
            size_t prev = search_pos - 1;
            while (prev > 0 && detail::is_utf8_cont(static_cast<uint8_t>(s[prev])))
                --prev;
            size_t tmp2 = prev;
            uint32_t prev_cp = detail::decode_utf8_at(s, tmp2);
            if (prev_cp == 0xFFFFFFFF || !is_ident_cont(prev_cp))
                break;
            if (is_ident_start(prev_cp)) {
                search_pos = prev;
                found_start = true;
                break;
            }
            search_pos = prev;
        }
        if (!found_start)
            return {};
        // Переустанавливаем offset на начало и начинаем заново
        offset = search_pos;
        tmp = offset;
        cp = detail::decode_utf8_at(s, tmp);
    }

    // Расширяем влево до начала идентификатора
    size_t begin = offset;
    while (begin > 0) {
        // откатываемся на один символ UTF8
        size_t prev = begin - 1;
        while (prev > 0 && detail::is_utf8_cont(static_cast<uint8_t>(s[prev])))
            --prev;
        size_t tmp2 = prev;
        uint32_t prev_cp = detail::decode_utf8_at(s, tmp2);
        if (prev_cp == 0xFFFFFFFF || !is_ident_cont(prev_cp))
            break;
        begin = prev;
    }

    // Расширяем вправо до конца идентификатора
    size_t end = tmp; // tmp уже продвинут после decode_utf8_at на первой проверке
    while (end < s.size()) {
        size_t tmp3 = end;
        uint32_t next_cp = detail::decode_utf8_at(s, tmp3);
        if (next_cp == 0xFFFFFFFF || !is_ident_cont(next_cp))
            break;
        end = tmp3;
    }

    return std::string_view(s.data() + begin, end - begin);
}

// -----------------------------------------------------------------------
//  Конвертация имени в C++ идентификатор и обратно
// -----------------------------------------------------------------------

inline std::string name_to_cpp(std::string_view name) {
    if (name.empty())
        return {};

    // Сканируем имя: ищем UTF8 символы, двоеточия
    bool has_utf8 = false;
    bool has_colon = false;
    bool only_russian_utf8 = true;    // все UTF8 символы — русские
    bool has_special_russian = false; // Ъ/Ь — теряют информацию при транслитерации
    bool has_ascii_alpha = false;     // есть ASCII буквы (a-z, A-Z) — конфликтуют с транслитерацией

    size_t i = 0;
    while (i < name.size()) {
        uint8_t b = static_cast<uint8_t>(name[i]);
        if (b < 0x80) {
            // ASCII
            if (b == ':')
                has_colon = true;
            else if (std::isalpha(static_cast<int>(b)))
                has_ascii_alpha = true;
            i += 1;
        } else {
            has_utf8 = true;
            uint32_t cp = detail::decode_utf8_at(name, i);
            if (cp == 0xFFFFFFFF)
                continue;
            if (!detail::is_russian_letter(cp))
                only_russian_utf8 = false;
            if (detail::is_hard_soft_sign(cp))
                has_special_russian = true;
        }
    }

    std::string result;

    if (!has_utf8 && !has_colon) {
        // Чистый ANSI C идентификатор
        result = "c_";
        result.append(name.data(), name.size());
    } else if (!has_utf8 && has_colon) {
        // Чистый C++ идентификатор (с двоеточиями)
        result = "cpp_";
        for (char ch : name) {
            if (ch == ':')
                result += '$';
            else
                result += ch;
        }
    } else if (has_utf8 && only_russian_utf8 && !has_ascii_alpha && !has_special_russian) {
        // Русский идентификатор (без Ъ/Ь, без ASCII букв) — транслитерация
        result = "ru_";
        size_t pos = 0;
        while (pos < name.size()) {
            uint8_t b = static_cast<uint8_t>(name[pos]);
            if (b < 0x80) {
                // ASCII символ передаётся как есть (кроме ':')
                if (b == ':')
                    result += '$'; // на всякий случай
                else
                    result += b;
                pos += 1;
            } else {
                uint32_t cp = detail::decode_utf8_at(name, pos);
                int idx = detail::translit_index(cp);
                if (idx >= 0) {
                    // Определяем, заглавная или строчная
                    bool upper = (cp >= 0x0410 && cp <= 0x042F) || cp == 0x0401;
                    result += upper ? detail::TRANSLIT_TABLE[idx].upper : detail::TRANSLIT_TABLE[idx].lower;
                }
            }
        }
    } else {
        // Другие UTF8 символы — HEX кодирование всех символов подряд
        result = "u8_";
        size_t pos = 0;
        auto hex_chars = [](std::string& out, uint8_t b) {
            static const char hex[] = "0123456789ABCDEF";
            out += hex[b >> 4];
            out += hex[b & 0x0F];
        };
        while (pos < name.size()) {
            uint8_t b = static_cast<uint8_t>(name[pos]);
            hex_chars(result, b);
            ++pos;
        }
    }

    return result;
}

inline std::string cpp_to_name(std::string_view cpp_name) {
    if (cpp_name.empty())
        return {};
    if (cpp_name.size() < 3)
        return {};

    auto check_prefix = [&](const char* prefix, size_t len) -> bool {
        return cpp_name.size() >= len && cpp_name.substr(0, len) == std::string_view(prefix, len);
    };

    // Префикс "c_" (2 символа)
    if (check_prefix("c_", 2)) {
        return std::string(cpp_name.substr(2));
    }

    // Префикс "cpp_" (4 символа)
    if (check_prefix("cpp_", 4)) {
        auto rest4 = cpp_name.substr(4);
        std::string result;
        result.reserve(rest4.size());
        for (char ch : rest4) {
            result += (ch == '$') ? ':' : ch;
        }
        return result;
    }

    // Префикс "ru_" (3 символа)
    if (check_prefix("ru_", 3)) {
        auto rest3 = cpp_name.substr(3);
        std::string result;
        size_t pos = 0;
        while (pos < rest3.size()) {
            // Сначала проверяем спецсимволы
            if (rest3[pos] == '$') {
                result += ':';
                pos += 1;
                continue;
            }
            // Пытаемся найти самое длинное совпадение с таблицей транслитерации
            int best_idx = -1;
            size_t best_len = 0;
            for (int ti = 0; ti < 33; ++ti) {
                const char* upper = detail::TRANSLIT_TABLE[ti].upper;
                const char* lower = detail::TRANSLIT_TABLE[ti].lower;
                size_t upper_len = std::strlen(upper);
                size_t lower_len = std::strlen(lower);
                // Проверяем upper (с заглавной)
                if (upper_len > 0 && pos + upper_len <= rest3.size()) {
                    if (rest3.substr(pos, upper_len) == std::string_view(upper, upper_len)) {
                        if (upper_len > best_len) {
                            best_len = upper_len;
                            best_idx = ti;
                        }
                    }
                }
                // Проверяем lower
                if (lower_len > 0 && pos + lower_len <= rest3.size()) {
                    if (rest3.substr(pos, lower_len) == std::string_view(lower, lower_len)) {
                        if (lower_len > best_len) {
                            best_len = lower_len;
                            best_idx = ti;
                        }
                    }
                }
            }
            if (best_idx >= 0) {
                uint32_t cp = detail::TRANSLIT_TABLE[best_idx].cp;
                // Проверяем, с какой буквы начинается: если первая буква в best совпадении заглавная —
                // значит и результат должен быть заглавным
                const char* upper_str = detail::TRANSLIT_TABLE[best_idx].upper;
                size_t upper_len = std::strlen(upper_str);
                bool is_upper = upper_len > 0 && rest3.substr(pos, std::min(upper_len, rest3.size() - pos)) == std::string_view(upper_str, upper_len);

                if (cp == 0x0401 || cp == 0x0451) {
                    // Ё/ё
                    char buf[4];
                    if (is_upper) {
                        buf[0] = 0xD0;
                        buf[1] = 0x81;
                    } else {
                        buf[0] = 0xD1;
                        buf[1] = 0x91;
                    }
                    result.append(buf, 2);
                } else {
                    char buf[4];
                    uint32_t target_cp;
                    if (is_upper)
                        target_cp = cp;
                    else
                        target_cp = cp + 0x20; // строчная
                    buf[0] = 0xD0 | static_cast<uint8_t>((target_cp >> 6) & 0x1F);
                    buf[1] = 0x80 | static_cast<uint8_t>(target_cp & 0x3F);
                    result.append(buf, 2);
                }
                pos += best_len;
            } else {
                // ASCII символ
                result += rest3[pos];
                pos += 1;
            }
        }
        return result;
    }

    // Префикс "u8_" (3 символа)
    if (check_prefix("u8_", 3)) {
        auto rest3 = cpp_name.substr(3);
        std::string result;
        for (size_t pos = 0; pos + 1 < rest3.size(); pos += 2) {
            auto hex_val = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9')
                    return static_cast<uint8_t>(c - '0');
                if (c >= 'A' && c <= 'F')
                    return static_cast<uint8_t>(c - 'A' + 10);
                if (c >= 'a' && c <= 'f')
                    return static_cast<uint8_t>(c - 'a' + 10);
                return 0;
            };
            uint8_t b = (hex_val(rest3[pos]) << 4) | hex_val(rest3[pos + 1]);
            result += static_cast<char>(b);
        }
        return result;
    }

    return {};
}

} // namespace trust::utils