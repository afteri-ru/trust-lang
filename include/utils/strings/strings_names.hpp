// include/utils/strings/strings_names.hpp
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

#include "utils/strings/strings_utf8.hpp"
#include "utils/strings/strings_ident.hpp"

namespace trust::utils {

inline std::string nameToCpp(std::string_view name) {
    if (name.empty()) {
        return {};
    }
    // Нативное имя (%add, %std::max) - это уже C++-символ рантайма: срезаем '%'
    // и возвращаем как есть (иначе сломается линковка).
    if (name.front() == '%') {
        return std::string(name.substr(1));
    }

    // Локальная переменная: ведущий '$' - сигнатура (аналог '%' у нативных), срезается
    // перед манглингом, чтобы локальная `$x` отображалась в `c_x` (а не в `c_$x`) и была
    // согласована с embed-ссылками `{% $x %}`. Спец-имена `$$` (родитель) не трогаем.
    if (name.front() == '$' && name.size() > 1 && name[1] != '$') {
        name.remove_prefix(1);
    }

    // Сканируем имя: ищем UTF8 символы, двоеточия
    bool has_utf8 = false;
    bool has_colon = false;
    bool only_russian_utf8 = true;    // все UTF8 символы - русские
    bool has_special_russian = false; // Ъ/Ь - теряют информацию при транслитерации
    bool has_ascii_alpha = false;     // есть ASCII буквы (a-z, A-Z) - конфликтуют с транслитерацией

    size_t i = 0;
    while (i < name.size()) {
        uint8_t b = static_cast<uint8_t>(name[i]);
        if (b < 0x80) {
            // ASCII
            if (b == ':') {
                has_colon = true;
            } else if (std::isalpha(static_cast<int>(b))) {
                has_ascii_alpha = true;
            }
            i += 1;
        } else {
            has_utf8 = true;
            uint32_t cp = detail::decode_utf8_at(name, i);
            if (cp == 0xFFFFFFFF) {
                continue;
            }
            if (!detail::isRussianLetter(cp)) {
                only_russian_utf8 = false;
            }
            if (detail::isHardSoftSign(cp)) {
                has_special_russian = true;
            }
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
            if (ch == ':') {
                result += '$';
            } else {
                result += ch;
            }
        }
    } else if (has_utf8 && only_russian_utf8 && !has_ascii_alpha && !has_special_russian) {
        // Русский идентификатор (без Ъ/Ь, без ASCII букв) - транслитерация
        result = "ru_";
        size_t pos = 0;
        while (pos < name.size()) {
            uint8_t b = static_cast<uint8_t>(name[pos]);
            if (b < 0x80) {
                // ASCII символ передаётся как есть (кроме ':')
                if (b == ':') {
                    result += '$'; // на всякий случай
                } else {
                    result += b;
                }
                pos += 1;
            } else {
                uint32_t cp = detail::decode_utf8_at(name, pos);
                int idx = detail::translitIndex(cp);
                if (idx >= 0) {
                    // Определяем, заглавная или строчная
                    bool upper = (cp >= 0x0410 && cp <= 0x042F) || cp == 0x0401;
                    result += upper ? detail::TRANSLIT_TABLE[idx].upper : detail::TRANSLIT_TABLE[idx].lower;
                }
            }
        }
    } else {
        // Другие UTF8 символы - HEX кодирование всех символов подряд
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

/// C++-путь namespace-контейнера: каждый сегмент через `nameToCpp` (`ns::sub` → `c_ns::c_sub`).
/// ЕДИНАЯ точка маппинга сегментов (кодоген типов и идентификаторов).
inline std::string cppNamespacePath(std::string_view ns) {
    std::string result;
    std::size_t pos = 0;
    while (pos <= ns.size()) {
        const std::size_t sep = ns.find("::", pos);
        const std::string_view seg = (sep == std::string_view::npos) ? ns.substr(pos) : ns.substr(pos, sep - pos);
        if (!seg.empty()) {
            if (!result.empty()) {
                result += "::";
            }
            result += nameToCpp(seg);
        }
        if (sep == std::string_view::npos) {
            break;
        }
        pos = sep + 2;
    }
    return result;
}

/// Единый C++-идентификатор для ОДНОГО сегмента имени (без `::`). Кодоген обязан манглить
/// квалифицированные имена через `qualifiedCppName`, а не передавать их в `nameToCpp`
/// (иначе `ns::x` молча превратился бы в `cpp_ns$$x`).
inline std::string cppIdent(std::string_view segment) {
    EXPECT(segment.find("::") == std::string_view::npos && "cppIdent expects a single name segment (no '::')");
    return nameToCpp(segment);
}

/// C++-имя trust-сущности по её (возможно квалифицированному) trust-имени:
/// `ns::f` → `c_ns::c_f`, `ns::Class::f` → `c_ns::c_Class::c_f`, `::f` → `c_f`.
/// Нативное имя (`%...`) - уже C++-символ: снимается только `%`.
inline std::string qualifiedCppName(std::string_view trustName) {
    if (!trustName.empty() && trustName.front() == '%') {
        return nameToCpp(trustName);
    }
    std::string_view qualified = trustName;
    if (qualified.rfind("::", 0) == 0) {
        qualified.remove_prefix(2); // глобальная область `::f`
    }
    const std::size_t sep = qualified.rfind("::");
    if (sep == std::string_view::npos) {
        return cppIdent(qualified);
    }
    return cppNamespacePath(qualified.substr(0, sep)) + "::" + cppIdent(qualified.substr(sep + 2));
}

/// C++-имя в DECLARATION-позиции: объявление всегда лежит в своей лексической области
/// (namespace/структура), поэтому эмитится КОРОТКОЕ имя (`ns::Class::field` → `c_field`).
/// Квалифицированный spelling в объявлении появляется из `@::`-раскрытия, а не из другой области.
inline std::string cppDeclName(std::string_view trustName) {
    if (!trustName.empty() && trustName.front() == '%') {
        return nameToCpp(trustName);
    }
    const std::size_t sep = trustName.rfind("::");
    const std::string_view shortName = (sep == std::string_view::npos) ? trustName : trustName.substr(sep + 2);
    return cppIdent(shortName);
}

inline std::string cppToName(std::string_view cpp_name) {
    if (cpp_name.empty()) {
        return {};
    }

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
                // Проверяем, с какой буквы начинается: если первая буква в best совпадении заглавная -
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
                    if (is_upper) {
                        target_cp = cp;
                    } else {
                        target_cp = cp + 0x20; // строчная
                    }
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
                if (c >= '0' && c <= '9') {
                    return static_cast<uint8_t>(c - '0');
                }
                if (c >= 'A' && c <= 'F') {
                    return static_cast<uint8_t>(c - 'A' + 10);
                }
                if (c >= 'a' && c <= 'f') {
                    return static_cast<uint8_t>(c - 'a' + 10);
                }
                return 0;
            };
            uint8_t b = (hex_val(rest3[pos]) << 4) | hex_val(rest3[pos + 1]);
            result += static_cast<char>(b);
        }
        return result;
    }

    // Имя без известного префикса (c_/cpp_/ru_/u8_) - нативное C++-имя: восстанавливаем маркер '%'.
    return "%" + std::string(cpp_name);
}

} // namespace trust::utils
