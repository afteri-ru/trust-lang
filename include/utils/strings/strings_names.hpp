// ЕДИНЫЙ ОБРАТИМЫЙ кодек trust-имени в C++-идентификатор.
// Безопасное ASCII-имя (`[A-Za-z_][A-Za-z0-9_]*`, не ключевое слово C++) отображается
// тождественно; иначе — маркер `_u_` плюс обратимая byte-схема (`_HH_` на небезопасный байт).
// Схема инъективна: `decodeIdent(encodeIdent(x)) == x`.
// Маркер обязан быть стандартным C++-идентификатором: `$` нестандартен (clang отвергает его
// при -pedantic), поэтому выбран `_u_`.
#pragma once

#include "utils/error.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace trust::utils {

namespace detail {

/// Значение шестнадцатеричного разряда или -1 для недопустимого символа.
[[nodiscard]] inline int hexDigitValue(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace detail

/// Префикс производного идентификатора для имён, требующих экранирования.
inline constexpr std::string_view kIdentEscapeMarker = "_u_";

/// Ключевые слова C++ (включая контекстные final/override/import/module), конфликтующие с
/// идентификаторами: такое trust-имя обязано экранироваться, иначе генерируемый C++ не скомпилируется.
[[nodiscard]] inline bool isCppKeywordIdent(std::string_view s) noexcept {
    static constexpr std::string_view kKeywords[] = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char8_t",
        "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue",
        "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit",
        "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new",
        "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register",
        "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
        "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned",
        "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq", "final", "override", "import", "module",
    };
    for (const std::string_view kw : kKeywords) {
        if (s == kw) {
            return true;
        }
    }
    return false;
}

/// Имя является "плоским" C++-идентификатором без экранирования.
[[nodiscard]] inline bool isPlainCppIdent(std::string_view s) noexcept {
    if (s.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(s.front());
    if (!(std::isalpha(first) || first == '_')) {
        return false;
    }
    for (const char c : s) {
        const auto b = static_cast<unsigned char>(c);
        if (!(std::isalnum(b) || b == '_')) {
            return false;
        }
    }
    return true;
}

/// Требуется ли экранирование сегмента (небезопасное имя / ключевое слово / уже похоже на маркер).
[[nodiscard]] inline bool needsIdentEscape(std::string_view s) noexcept {
    return !isPlainCppIdent(s) || s.starts_with(kIdentEscapeMarker) || isCppKeywordIdent(s);
}

/// Закодировать ОДИН сегмент trust-имени (UTF-8, без `::`) в C++-идентификатор. Обратимо.
[[nodiscard]] inline std::string encodeIdent(std::string_view name) {
    if (name.empty()) {
        return {};
    }
    if (!needsIdentEscape(name)) {
        return std::string(name);
    }
    static const char hex[] = "0123456789ABCDEF";
    std::string out(kIdentEscapeMarker);
    out.reserve(name.size() + kIdentEscapeMarker.size());
    for (const char ch : name) {
        const auto b = static_cast<unsigned char>(ch);
        if ((b >= '0' && b <= '9') || (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z')) {
            out += static_cast<char>(b);
        } else {
            out += '_';
            out += hex[b >> 4];
            out += hex[b & 0x0F];
            out += '_';
        }
    }
    return out;
}

/// Обратное преобразование: C++-идентификатор -> trust-имя (UTF-8). ЕДИНСТВЕННАЯ точка декода.
[[nodiscard]] inline std::string decodeIdent(std::string_view ident) {
    if (!ident.starts_with(kIdentEscapeMarker)) {
        return std::string(ident);
    }
    const std::string_view payload = ident.substr(kIdentEscapeMarker.size());
    std::string out;
    out.reserve(payload.size());
    std::size_t i = 0;
    while (i < payload.size()) {
        if (payload[i] != '_') {
            out += payload[i];
            ++i;
            continue;
        }
        EXPECT(i + 3 < payload.size() && payload[i + 3] == '_' && "decodeIdent: malformed escape sequence in encoded identifier");
        const int hi = detail::hexDigitValue(payload[i + 1]);
        const int lo = detail::hexDigitValue(payload[i + 2]);
        EXPECT(hi >= 0 && lo >= 0 && "decodeIdent: non-hex digit in escape sequence");
        out += static_cast<char>((hi << 4) | lo);
        i += 4;
    }
    return out;
}

/// trust-имя (возможно квалифицированное `ns::f`/`::f`, локальное `$x`) -> C++-имя.
/// Ведущий `$` срезается (локальная переменная), `::` разбирается по сегментам.
[[nodiscard]] inline std::string nameToCpp(std::string_view name);

/// C++-путь namespace-контейнера: каждый сегмент через `encodeIdent` (`ns::sub` -> `ns::sub`).
/// ЕДИНАЯ точка маппинга сегментов (кодоген типов и идентификаторов).
[[nodiscard]] inline std::string cppNamespacePath(std::string_view ns) {
    std::string result;
    std::size_t pos = 0;
    while (pos <= ns.size()) {
        const std::size_t sep = ns.find("::", pos);
        const std::string_view seg = (sep == std::string_view::npos) ? ns.substr(pos) : ns.substr(pos, sep - pos);
        if (!seg.empty()) {
            if (!result.empty()) {
                result += "::";
            }
            result += encodeIdent(seg);
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
/// (иначе сегмент с `::` был бы экранирован целиком). Ведущий `$`-сигил локальной переменной
/// срезается (локальная `$x` → `x`, согласовано с embed-ссылками `{% $x %}`); спец-имя `$$`
/// (родитель) не трогаем.
[[nodiscard]] inline std::string cppIdent(std::string_view segment) {
    EXPECT(segment.find("::") == std::string_view::npos && "cppIdent expects a single name segment (no '::')");
    if (!segment.empty() && segment.front() == '$' && segment.size() > 1 && segment[1] != '$') {
        segment.remove_prefix(1);
    }
    return encodeIdent(segment);
}

/// C++-имя trust-сущности по её (возможно квалифицированному) trust-имени:
/// `ns::f` -> `ns::f`, `ns::Class::f` -> `ns::Class::f`, `::f` -> `f`.
[[nodiscard]] inline std::string qualifiedCppName(std::string_view trustName) {
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
/// (namespace/структура), поэтому эмитится КОРОТКОЕ имя (`ns::Class::field` -> `field`).
[[nodiscard]] inline std::string cppDeclName(std::string_view trustName) {
    const std::size_t sep = trustName.rfind("::");
    const std::string_view shortName = (sep == std::string_view::npos) ? trustName : trustName.substr(sep + 2);
    return cppIdent(shortName);
}

[[nodiscard]] inline std::string nameToCpp(std::string_view name) {
    if (!name.empty() && name.front() == '$' && name.size() > 1 && name[1] != '$') {
        name.remove_prefix(1);
    }
    if (name.find("::") != std::string_view::npos) {
        return qualifiedCppName(name);
    }
    return encodeIdent(name);
}

} // namespace trust::utils
