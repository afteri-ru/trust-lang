#pragma once

// include/semantic/type_inference.hpp
// Языковая семантика типов выражений: типизация литералов и вывод типа результата
// бинарных операций по семантике C++ (обычные арифметические преобразования:
// Int16+Int16 → Int32, '//'/'//=' → Int64, Compare/Logical → Bool, std::any-операнды).
// Числовое продвижение (общий арифметический тип, продвижение одиночного операнда,
// float по разрядности) — единый TypeId-aware источник `types/promotion.hpp`.

#include "ast/token.hpp"
#include "ast/ast_nodes.hpp"
#include "types/group.hpp"
#include "types/int_literal.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "utils/operators.hpp"

#include <string_view>

namespace trust {

// ── Парсинг беззнакового целого литерала ────────────────
// Единый хелпер для literalType и проверки сужения литерала в целевую цель
// (intFitsTarget). base 0 — десятичные/шестнадцатеричные/восьмеричные литералы C++.
// Текст с ведущим '-' или не являющийся целым числом → false (не типизируем).
inline bool parseDecimalUInt(std::string_view text, unsigned long long& out) noexcept {
    if (text.empty() || text[0] == '-') {
        return false;
    }
    try {
        std::size_t pos = 0;
        out = std::stoull(std::string(text), &pos, 0);
        return pos == text.size();
    } catch (...) {
        return false;
    }
}

// ── Диапазоны целых литералов ─────────────────────────────
// Границы целых типов и соответствие ширина↔тип вынесены в единый источник
// `types/int_literal.hpp` (fitsIntegerValue / intTypeForWidth / intTypeForLiteral);
// здесь остаётся только операторная семантика (литералы, Compare/Logical, any, //).

// ── Тип литерала ─────────────────────────────────────────
// IntLiteral → минимальный конкретный знаковый Int, вмещающий значение (Int8/16/32/64);
// 0 и 1 → Bool (логические литералы). FloatLiteral → Float64 (наибольший поддерживаемый).
// StrChar ('…', узкая строка) → StrChar; StrWide ("…", широкая строка) → StrWide.
// Прочие/неизвестные → INVALID_TYPE_ID.
inline TypeId literalType(const Literal& lit, const TypeRegistry& reg) {
    switch (lit.kind()) {
    case ParserToken::Kind::IntLiteral: {
        unsigned long long v = 0;
        if (!parseDecimalUInt(lit.text(), v)) {
            return INVALID_TYPE_ID;
        }
        // 0 и 1 — логические литералы (Bool); остальные — минимальный знаковый Int, вмещающий значение.
        if (v == 0ULL || v == 1ULL) {
            return reg.getType(type::Bool);
        }
        return intTypeForLiteral(reg, v); // единая таблица ширина/границ (types/int_literal.hpp)
    }
    case ParserToken::Kind::FloatLiteral:
        return reg.getType(type::Float64);
    case ParserToken::Kind::StrChar:
        // Строка в одинарных кавычках '…' (STRCHAR) — узкая → StrChar.
        return reg.getType(type::StrChar);
    case ParserToken::Kind::StrWide:
        // Строка в двойных кавычках "…" (STRWIDE) — широкая → StrWide.
        return reg.getType(type::StrWide);
    case ParserToken::Kind::RationalLiteral:
        // Рациональный литерал `num\den` (отдельная лексема RATIONAL) → Rational.
        return reg.getType(type::Rational);
    default:
        return INVALID_TYPE_ID;
    }
}

// ── Тип результата бинарной операции ─────────────────────
// По обычным арифметическим преобразованиям C++:
//   * Compare/Logical → Bool;
//   * MathOp "//" (целочисленное деление) → Int64 (кодогенерация кастует операнды к int64_t);
//   * один операнд std::any + конкретный числовой → продвинутый конкретный (для any_cast);
//   * оба any → INVALID (тип невыводим);
//   * числа (Integers/Unsigned/Numbers): если есть float — более широкая float-группа;
//     иначе целое: при наличии 64-битного операнда → Int64, иначе → Int32;
//   * не-арифметические/неизвестные операнды → INVALID_TYPE_ID.
inline TypeId resultTypeBinary(ParserToken::Kind kind, std::string_view op, TypeId lhs, TypeId rhs, const TypeRegistry& reg) {
    if (kind == ParserToken::Kind::CompareOp || kind == ParserToken::Kind::LogicalOp) {
        return reg.getType(type::Bool);
    }
    if (lhs == INVALID_TYPE_ID || rhs == INVALID_TYPE_ID) {
        return INVALID_TYPE_ID;
    }
    // Целочисленное деление // и //= → результат Int64 (см. кодогенерацию: static_cast<int64_t>).
    if (utils::isIntDivOp(op)) {
        return reg.getType(type::Int64);
    }

    const TypeId lc = reg.getCanonicalTypeId(lhs);
    const TypeId rc = reg.getCanonicalTypeId(rhs);
    const Group lg = getGroup(getKindFromId(lc));
    const Group rg = getGroup(getKindFromId(rc));

    const bool lAny = isAnyType(lhs, reg);
    const bool rAny = isAnyType(rhs, reg);
    const bool lNum = isArithmeticGroup(lg);
    const bool rNum = isArithmeticGroup(rg);

    // Один операнд std::any + конкретный числовой → результат = продвинутый конкретный.
    if (lAny && rNum) {
        return promoteSingleNumeric(reg, rc);
    }
    if (rAny && lNum) {
        return promoteSingleNumeric(reg, lc);
    }
    if (lAny || rAny) {
        return INVALID_TYPE_ID; // оба any — тип невыводим
    }
    if (!lNum || !rNum) {
        return INVALID_TYPE_ID;
    }

    // Числовые операнды → общий арифметический тип (C++ usual arithmetic conversions).
    return commonArithmeticType(reg, lc, rc);
}

} // namespace trust
