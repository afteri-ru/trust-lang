#pragma once

// include/types/int_literal.hpp
// Единый источник диапазонов целых литералов и соответствия ширина↔тип.
// Вынесен в types из semantic/type_inference.hpp, чтобы границы целых жили в одном месте
// рядом с реестром типов (TypeRegistry хранит ширину data для встроенных типов, но НЕ
// границы диапазонов и НЕ «тип по ширине»). Используется:
//   - literalType (выбор минимального вмещающего знакового Int по значению литерала);
//   - intFitsTarget / checkAssignmentNarrowing (влезает ли литерал в целевой целый тип).

#include "types/group.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace trust {

// -- Срезание разделителей разрядов '_' в числовом литерале ---------
// По грамматике '_' допустимы только между цифрами (integer {digit}([_]?{digit}+)*),
// поэтому удаление всех '_' безопасно. Единая точка: применяется ко ВСЕМ числам
// (типизация литералов, сужение, индексные проверки, размерности массива, enum-значения).
inline std::string stripDigitSeparators(std::string_view text) {
    std::string s(text);
    s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
    return s;
}

// -- Парсинг беззнакового целого литерала ----------------
// Единый хелпер для intLiteralType и проверки сужения литерала в целевую цель
// (intFitsTarget). base 0 - десятичные/шестнадцатеричные/восьмеричные литералы C++.
// Текст с ведущим '-' или не являющийся целым числом → false (не типизируем).
// Разделители разрядов '_' срезаются (stripDigitSeparators) до парсинга.
inline bool parseDecimalUInt(std::string_view text, unsigned long long& out) noexcept {
    if (text.empty() || text[0] == '-') {
        return false;
    }
    try {
        std::string stripped = stripDigitSeparators(text);
        std::size_t pos = 0;
        out = std::stoull(stripped, &pos, 0);
        return pos == stripped.size();
    } catch (...) {
        return false;
    }
}

// -- Единый разбор десятичного целочисленного литерала ---------
// Структура результата: знак + магнитуд. Магнитуд хранится в unsigned long long (точный,
// если !exceedsUInt64); при переполнении (> UINT64_MAX) exceedsUInt64=true и magnitude
// насыщается (→ BigInteger). Этого достаточно для вывода типа (нужно лишь «влезает ли в
// знаковый Int64», а не точное сверхразрядное значение) и не тянет GMP в types-слой.
struct ParsedIntLiteral {
    bool negative = false;
    unsigned long long magnitude = 0;
    bool exceedsUInt64 = false;
};

// ЕДИНСТВЕННЫЙ разбор литерала: срезает '_', знак и ведущие нули; парсит магнитуд.
// false — пустой/не-цифра (не целочисленный литерал). Используется всеми функциями
// типизации литералов (intFitsTarget / intTypeForSignedMagnitude / intLiteralType).
inline bool parseIntegerLiteral(std::string_view text, ParsedIntLiteral& out) noexcept {
    std::string s = stripDigitSeparators(text);
    if (s.empty()) {
        return false;
    }
    out.negative = s[0] == '-';
    if (out.negative) {
        s.erase(s.begin());
    }
    const std::size_t first = s.find_first_not_of('0');
    s = (first == std::string_view::npos) ? std::string("0") : s.substr(first);
    out.exceedsUInt64 = false;
    out.magnitude = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
        const unsigned long long d = static_cast<unsigned long long>(c - '0');
        if (out.magnitude > (std::numeric_limits<unsigned long long>::max() - d) / 10ULL) {
            out.magnitude = std::numeric_limits<unsigned long long>::max();
            out.exceedsUInt64 = true;
            return true; // магнитуд > UINT64_MAX
        }
        out.magnitude = out.magnitude * 10ULL + d;
    }
    return true;
}

// -- Влезает ли магнитуд (с учётом знака) в знаковый Int ширины width (8/16/32/64) --
// Положительный |v| <= 2^(w-1)-1 (INTxx_MAX), отрицательный |v| <= 2^(w-1) (=|INTxx_MIN|).
// Границы из std::numeric_limits. Единый предикат для intTypeForSignedMagnitude / intFitsTarget.
inline bool fitsSignedIntMagnitude(const ParsedIntLiteral& v, uint8_t width) noexcept {
    if (v.exceedsUInt64) {
        return false; // магнитуд > UINT64_MAX - не влезает ни в один знаковый Int
    }
    const unsigned long long int64Max = static_cast<unsigned long long>(std::numeric_limits<int64_t>::max());
    const unsigned long long int64MinMag = static_cast<unsigned long long>(std::numeric_limits<int64_t>::min());
    const unsigned long long bound = v.negative ? (int64MinMag >> (64 - width)) : (int64Max >> (64 - width));
    return v.magnitude <= bound;
}

// -- Знаковый целый тип (Int8/16/32/64) по ширине в битах ----
inline TypeId intTypeForWidth(const TypeRegistry& reg, uint8_t width) {
    switch (width) {
    case 8:
        return reg.getType(type::Int8);
    case 16:
        return reg.getType(type::Int16);
    case 32:
        return reg.getType(type::Int32);
    default:
        return reg.getType(type::Int64);
    }
}

// -- Минимальный знаковый Int по знак-учтённому магнитуду (ЕДИНСТВЕННЫЙ выбор типа целого) --
// Общий core для intLiteralType (текст) и intTypeForLiteral (значение). Не помещающийся в
// знаковый Int64 (магнитуд > INT64_MAX либо |INT64_MIN| для отрицательного) → BigInteger.
inline TypeId intTypeForSignedMagnitude(const TypeRegistry& reg, const ParsedIntLiteral& v) {
    constexpr uint8_t kSignedWidths[] = {8, 16, 32, 64};
    for (const uint8_t w : kSignedWidths) {
        if (fitsSignedIntMagnitude(v, w)) {
            return intTypeForWidth(reg, w);
        }
    }
    return reg.getType(type::BigInteger);
}

// -- Минимальный знаковый Int, вмещающий беззнаковое значение (для enum: номер члена/порядок) --
inline TypeId intTypeForLiteral(const TypeRegistry& reg, unsigned long long value) {
    return intTypeForSignedMagnitude(reg, ParsedIntLiteral{false, value, false});
}

// -- Конкретный тип целочисленного литерала по тексту ----
// По тексту литерала (с '_' и возможным '-') возвращает конкретный тип из реестра.
// Влезающий → минимальный знаковый Int (знак-учёт), сверхразрядный
// (магнитуд > 2^63) → BigInteger. Отрицательные литералы типизируются
// (`-5` → Int8, `-9223372036854775808` → Int64, `-9223372036854775809` → BigInteger).
// Цифры 0 и 1 НЕ выводятся как Bool: автовыведение всегда даёт целый тип (0/1 → Int8);
// Bool из числа возможен ТОЛЬКО при явной аннотации литерала `0 :Bool` / `1 :Bool`
// (см. annotatedLiteralType/typeExpr).
inline TypeId intLiteralType(const TypeRegistry& reg, std::string_view text) {
    ParsedIntLiteral v;
    if (!parseIntegerLiteral(text, v)) {
        return INVALID_TYPE_ID;
    }
    return intTypeForSignedMagnitude(reg, v);
}

} // namespace trust
