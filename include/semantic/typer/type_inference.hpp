#pragma once

// Языковая семантика типов выражений: типизация литералов и вывод типа результата
// бинарных операций по семантике C++ (обычные арифметические преобразования:
// Int16+Int16 → Int32, '//'/'//=' → Int64, Compare/Logical → Bool, std::any-операнды).
// Числовое продвижение (общий арифметический тип, продвижение одиночного операнда,
// float по разрядности) - единый TypeId-aware источник `types/promotion.hpp`.

#include "types/type_read.hpp"
#include "ast/token.hpp"
#include "ast/ast_nodes.hpp"
#include "types/group.hpp"
#include "utils/partial_switch.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "ast/binary_op.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace trust {

// Границы целых типов и соответствие ширина↔тип заданы единым источником
// `types/int_literal.hpp` (fitsSignedIntMagnitude / intTypeForSignedMagnitude / intTypeForWidth /
// intTypeForLiteral / intLiteralType); здесь остаётся только операторная семантика
// (литералы, Compare/Logical, any, //).

// IntLiteral → единый конвертер intLiteralType: минимальный конкретный знаковый Int,
// вмещающий значение (Int8/16/32/64); 0 и 1 → Int8 (цифры НЕ выводятся как Bool; Bool —
// только явная аннотация `0 :Bool`); сверхразрядный (модуль > INT64_MAX) → BigInteger.
// FloatLiteral → Float64 (наибольший поддерживаемый).
// StrChar ("…", узкая строка) → StrChar; StrWide ('…', широкая строка) → StrWide.
// Прочие/неизвестные → INVALID_TYPE_ID.
inline TypeId literalType(const Literal& lit, const TypeReadModel& reg) {
    // Частичный предикат: только литеральные kinds; прочие - INVALID_TYPE_ID (осознанно).
    TRUST_PARTIAL_ENUM_SWITCH_BEGIN
    switch (lit.kind()) {
    case ParserToken::Kind::IntLiteral:
        // Единый конвертер (types/int_literal.hpp): включает выбор Int/Bool/BigInteger.
        return intLiteralType(reg, lit.text());
    case ParserToken::Kind::FloatLiteral:
        return reg.getType(type::Float64);
    case ParserToken::Kind::StrChar:
        // Строка в двойных кавычках "…" (STRCHAR) - узкая → StrChar.
        return reg.getType(type::StrChar);
    case ParserToken::Kind::StrWide:
        // Строка в одинарных кавычках '…' (STRWIDE) - широкая → StrWide.
        return reg.getType(type::StrWide);
    case ParserToken::Kind::RationalLiteral:
        // Рациональный литерал `num\den` (отдельная лексема RATIONAL) → Rational.
        return reg.getType(type::Rational);
    default:
        return INVALID_TYPE_ID;
    }
    TRUST_PARTIAL_ENUM_SWITCH_END
}

// По обычным арифметическим преобразованиям C++:
//   * Compare/Logical → Bool;
//   * BigInteger/Rational (kArbitraryPrecision) - отдельная ветка (кастомные runtime-типы):
//     `//` для них недоступно; +/-/*// → точный тип (BigInteger/Rational; Rational "шире");
//   * MathOp "//" (целочисленное деление) → Int64 (кодогенерация кастует операнды к int64_t);
//   * один операнд std::any + конкретный числовой → продвинутый конкретный (для any_cast);
//   * оба any → INVALID (тип невыводим);
//   * числа (Integers/Unsigned/Numbers): если есть float - более широкая float-группа;
//     иначе целое: при наличии 64-битного операнда → Int64, иначе → Int32;
//   * не-арифметические/неизвестные операнды → INVALID_TYPE_ID.
inline TypeId resultTypeBinary(ParserToken::Kind kind, BinaryOp op, TypeId lhs, TypeId rhs, const TypeReadModel& reg) {
    if (kind == ParserToken::Kind::CompareOp || kind == ParserToken::Kind::LogicalOp) {
        return reg.getType(type::Bool);
    }
    if (lhs == INVALID_TYPE_ID || rhs == INVALID_TYPE_ID) {
        return INVALID_TYPE_ID;
    }

    const TypeId lc = reg.store().desugaredTypeId(lhs);
    const TypeId rc = reg.store().desugaredTypeId(rhs);
    const Group lg = typeGroupOf(lc);
    const Group rg = typeGroupOf(rc);

    // BigInteger/Rational: отдельная ветка (см. arbitraryPrecisionArithmeticType). Целочисленное
    // деление `//` для них недоступно (нет int64-каста); смешивание с float - ошибка.
    const bool lAP = lg == Group::kArbitraryPrecision;
    const bool rAP = rg == Group::kArbitraryPrecision;
    if (lAP || rAP) {
        if (isIntDivOp(op)) {
            return INVALID_TYPE_ID;
        }
        return arbitraryPrecisionArithmeticType(reg, lc, rc);
    }

    // Целочисленное деление // и //= → результат Int64 (см. кодогенерацию: static_cast<int64_t>).
    if (isIntDivOp(op)) {
        return reg.getType(type::Int64);
    }

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
        return INVALID_TYPE_ID; // оба any - тип невыводим
    }
    if (!lNum || !rNum) {
        return INVALID_TYPE_ID;
    }

    // Числовые операнды → общий арифметический тип (C++ usual arithmetic conversions).
    return commonArithmeticType(reg, lc, rc);
}

} // namespace trust
