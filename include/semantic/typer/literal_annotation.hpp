#pragma once

// ЕДИНСТВЕННЫЙ решатель типа литерала с постфиксной аннотацией `literal :Type`:
// чистый (без диагностики), возвращает тип ИЛИ INVALID + причину для репорта.
// Невалидная аннотация НЕ даёт fallback на «сырой» номинал — ошибку репортит typeExpr.

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "types/call.hpp"
#include "types/names.hpp"
#include "types/type_id.hpp"
#include "types/type_read.hpp"
#include "types/typekind.hpp"

#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

enum class LiteralAnnotProblem { None, UnknownType, RationalExpected, BoolNot01, IntOverflow, NonNumeric };

struct LiteralAnnotResult {
    TypeId type = INVALID_TYPE_ID;
    LiteralAnnotProblem problem = LiteralAnnotProblem::None;
};

// forward-декларация: annotatedLiteralType использует intFitsTarget ниже.
inline bool intFitsTarget(std::string_view text, TypeKind targetKind) noexcept;

inline LiteralAnnotResult annotatedLiteralType(const Literal& lit, std::optional<TypeId> ann, const TypeReadModel& reg) {
    if (!ann.has_value() || *ann == INVALID_TYPE_ID) {
        // nullopt — тип не найден (resolveTypeRef диагностику НЕ формирует, pass.hpp);
        // INVALID-значение resolveTypeRef уже диагностировал сам (ref/template-ошибки).
        return {INVALID_TYPE_ID, LiteralAnnotProblem::UnknownType};
    }
    const TypeId ac = reg.store().desugaredTypeId(*ann);
    const TypeId rat = reg.store().desugaredTypeId(reg.getType(type::Rational));
    if (lit.kind() == ParserToken::Kind::RationalLiteral) {
        return (ac == rat) ? LiteralAnnotResult{*ann, LiteralAnnotProblem::None} : LiteralAnnotResult{INVALID_TYPE_ID, LiteralAnnotProblem::RationalExpected};
    }
    const TypeKind ak = getTypeKind(ac);
    const Group ag = typeGroupOf(ak);
    const TypeId bi = reg.store().desugaredTypeId(reg.getType(type::BigInteger));
    if (ac == bi || ac == rat) {
        return {*ann, LiteralAnnotProblem::None};
    }
    const TypeId boolT = reg.store().desugaredTypeId(reg.getType(type::Bool));
    if (ac == boolT) {
        const std::string clean = stripDigitSeparators(lit.text());
        return (clean == "0" || clean == "1") ? LiteralAnnotResult{*ann, LiteralAnnotProblem::None}
                                              : LiteralAnnotResult{INVALID_TYPE_ID, LiteralAnnotProblem::BoolNot01};
    }
    if (ag == Group::kIntegers || ag == Group::kUnsigned) {
        const bool fits = (lit.kind() == ParserToken::Kind::IntLiteral) && intFitsTarget(lit.text(), ak);
        return fits ? LiteralAnnotResult{*ann, LiteralAnnotProblem::None} : LiteralAnnotResult{INVALID_TYPE_ID, LiteralAnnotProblem::IntOverflow};
    }
    if (ag == Group::kNumbers) {
        return {*ann, LiteralAnnotProblem::None};
    }
    return {INVALID_TYPE_ID, LiteralAnnotProblem::NonNumeric};
}

// Знак-учитывающая проверка влезания целого литерала в целевой тип (разбор — int_literal.hpp).
inline bool intFitsTarget(std::string_view text, TypeKind targetKind) noexcept {
    const Group g = typeGroupOf(targetKind);
    if (g != Group::kIntegers && g != Group::kUnsigned) {
        return true; // не-целая цель (float) — целочисленный литерал считается безопасным
    }
    const uint8_t width = typeDataOf(targetKind);
    ParsedIntLiteral v;
    if (!parseIntegerLiteral(text, v)) {
        return false; // не-цифра/пустой
    }
    if (g == Group::kUnsigned) {
        if (v.negative || v.exceedsUInt64) {
            return false; // отрицательное или > UINT64_MAX не влезает в беззнаковое
        }
        return v.magnitude <= (static_cast<unsigned long long>(std::numeric_limits<uint64_t>::max()) >> (64 - width));
    }
    return fitsSignedIntMagnitude(v, width);
}

} // namespace trust
