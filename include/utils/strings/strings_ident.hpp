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

namespace trust::utils {

inline std::string_view extractName(std::string_view s, size_t offset) {
    if (s.empty() || offset >= s.size()) {
        return {};
    }

    // Если offset указывает на continuation-байт UTF8, откатываемся
    // к началу последовательности
    while (offset > 0 && detail::isUtf8Cont(static_cast<uint8_t>(s[offset]))) {
        --offset;
    }

    // Проверяет, может ли символ быть первым в идентификаторе
    auto is_ident_start = [](uint32_t cp) -> bool {
        if (cp < 0x80) {
            return std::isalpha(static_cast<int>(cp)) || cp == '_' || cp == ':';
        }
        return true; // UTF8 буквы
    };
    // Проверяет, может ли символ быть внутри идентификатора (первые и последующие)
    auto is_ident_cont = [](uint32_t cp) -> bool {
        if (cp < 0x80) {
            return std::isalnum(static_cast<int>(cp)) || cp == '_' || cp == ':';
        }
        return true;
    };

    size_t pos = offset;
    uint32_t cp;
    size_t tmp = pos;
    cp = detail::decode_utf8_at(s, tmp);
    if (cp == 0xFFFFFFFF) {
        return {};
    }

    // Если символ не может быть началом, но может быть продолжением - ищем начало слева
    if (!is_ident_start(cp)) {
        if (!is_ident_cont(cp)) {
            return {};
        }
        // Ищем начало идентификатора слева
        size_t search_pos = offset;
        bool found_start = false;
        while (search_pos > 0) {
            size_t prev = search_pos - 1;
            while (prev > 0 && detail::isUtf8Cont(static_cast<uint8_t>(s[prev]))) {
                --prev;
            }
            size_t tmp2 = prev;
            uint32_t prev_cp = detail::decode_utf8_at(s, tmp2);
            if (prev_cp == 0xFFFFFFFF || !is_ident_cont(prev_cp)) {
                break;
            }
            if (is_ident_start(prev_cp)) {
                search_pos = prev;
                found_start = true;
                break;
            }
            search_pos = prev;
        }
        if (!found_start) {
            return {};
        }
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
        while (prev > 0 && detail::isUtf8Cont(static_cast<uint8_t>(s[prev]))) {
            --prev;
        }
        size_t tmp2 = prev;
        uint32_t prev_cp = detail::decode_utf8_at(s, tmp2);
        if (prev_cp == 0xFFFFFFFF || !is_ident_cont(prev_cp)) {
            break;
        }
        begin = prev;
    }

    // Расширяем вправо до конца идентификатора
    size_t end = tmp; // tmp уже продвинут после decode_utf8_at на первой проверке
    while (end < s.size()) {
        size_t tmp3 = end;
        uint32_t next_cp = detail::decode_utf8_at(s, tmp3);
        if (next_cp == 0xFFFFFFFF || !is_ident_cont(next_cp)) {
            break;
        }
        end = tmp3;
    }

    return std::string_view(s.data() + begin, end - begin);
}

/// Срезает ведущий '%' (native-маркер) у trust-имени. Нативные имена (%add, %std::max)
/// - уже C++-символы рантайма; '%' используется для маркировки и при кодогенерации
/// срезается, а имя остаётся как есть. Единый источник среза для транспилятора
/// (расчёт длины trust-range) и анализатора (funcShortName).
inline std::string_view stripNativePrefix(std::string_view name) noexcept {
    if (!name.empty() && name.front() == '%') {
        name.remove_prefix(1);
    }
    return name;
}

/// Срезает ведущий '@' (сигил макроса) у trust-имени. Имена макросов в потоке токенов могут
/// быть с '@' (@return, @trust_pre) или без (return, trust_pre); единый источник среза для
/// классификации макросов (форматтер, LSP).
inline std::string_view stripMacroSigil(std::string_view name) noexcept {
    if (!name.empty() && name.front() == '@') {
        name.remove_prefix(1);
    }
    return name;
}

/// Срезает ведущий ':' (сигил типа) у текстового имени типа (`:combi` → `combi`). Единый
/// источник среза для мест, где тип представлен текстом (резолв типа, разбор наборов типов).
inline std::string_view stripTypeSigil(std::string_view name) noexcept {
    if (!name.empty() && name.front() == ':') {
        name.remove_prefix(1);
    }
    return name;
}

/// Полный ключ метода кодирует нативность (ведущий '%') и константность (хвостовой '^'),
/// напр. "%count^". bare-имя - ключ без '%'/'^' (идентичность метода при поиске и проверке
/// дубликатов). '%' срезается единым источником stripNativePrefix; '^' - хвостовой
/// квалификатор ключа-идентичности (это НЕ нормализация AST-текста).
inline std::string bareName(std::string_view name) {
    std::string s(stripNativePrefix(name));
    if (!s.empty() && s.back() == '^') {
        s.pop_back();
    }
    return s;
}
/// Имя константное (хвостовой '^') - квалификатор ключа-идентичности.
inline bool isConstName(std::string_view name) {
    return !name.empty() && name.back() == '^';
}
/// Имя - СТАТИЧЕСКИЙ член класса (содержит '::'; статика задаётся `@::name` → ns::Class::name,
/// ключ = полное имя с '::'). Проверяется по ЗАРЕГИСТРИРОВАННОМУ имени (ключу), а не по имени члена.
inline bool isStaticName(std::string_view name) {
    return name.find("::") != std::string_view::npos;
}

/// Имя модуля (\file, \\group, \\\pkg): ведущий обратный слэш.
inline bool isModuleName(std::string_view name) noexcept {
    return !name.empty() && name.front() == '\\';
}

/// Локальное имя ($name или name$): сигил локальности в начале или в конце.
inline bool isLocalName(std::string_view name) noexcept {
    return !name.empty() && (name.front() == '$' || name.back() == '$');
}

/// Имя макроса (@name): ведущий '@'.
inline bool isMacroName(std::string_view name) noexcept {
    return !name.empty() && name.front() == '@';
}

/// Имя с ЛЮБЫМ служебным сигилом (локальное, макрос, тип, native, модуль).
inline bool isLocalAnyName(std::string_view name) noexcept {
    return !name.empty() && (name.front() == '$' || name.front() == '@' || name.front() == ':' || name.front() == '%' || name.front() == '\\');
}

/// Зарезервированное служебное имя (none/parent/args/last-result/global-scope и т.п.).
inline bool isTrustReservedName(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    if (name.size() > 3 || !(name[0] == '$' || name[0] == '@' || name[0] == '%')) {
        return name == "_";
    }
    return name == "$" || name == "@" || name == "%" || name == "$$" || name == "@$" || name == "$^" || name == "$?" || name == "@::" || name == "$*" ||
           name == "$#";
}

} // namespace trust::utils
