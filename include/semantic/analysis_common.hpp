#pragma once

// include/semantic/analysis_common.hpp
// Общие свободные хелперы однопроходной семантики (NameResolutionPass), разделяемые
// компонентами-анализаторами (DeclAnalyzer/ExprTyper/AccessResolver/TrustAnalyzer).
// Вынесены из монолитного name_resolution.cpp, чтобы анализаторы компилировались независимо.

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "semantic/format_check.hpp"
#include "semantic/type_inference.hpp"
#include "types/int_literal.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "utils/strings.hpp"

#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

// forward-декларация (определение ниже в этом же заголовке): annotatedLiteralType использует
// intFitsTarget для проверки влезания аннотированного целого литерала.
inline bool intFitsTarget(std::string_view text, TypeKind targetKind) noexcept;

/// ЕДИНСТВЕННЫЙ решатель типа литерала с постфиксной аннотацией `literal :Type` (чистый, без
/// диагностики). Возвращает выбранный тип ИЛИ INVALID + причину для репорта. ann - уже
/// резолвленная аннотация (nullopt ⇒ неизвестный тип); резолв имени делает вызывающий.
///
/// Правила (единые для всех вызовов - typeExpr/reportLiteralAnnotProblem с репортом и
/// провизионные dictElementType/resolvedType БЕЗ репорта):
///   * RationalLiteral `num\den` - только `:Rational` (иначе RationalExpected);
///   * IntLiteral/FloatLiteral: :BigInteger/:Rational - любой (в т.ч. сверхразрядный);
///   * :Bool - только 0/1 (BoolNot01);
///   * фикс. целые/беззнаковые - значение должно влезать, intFitsTarget знак-учитывающий
///     (IntOverflow);
///   * float (:Float32/:Float64 и числа) - как есть;
///   * прочее (не-числовая/не-Bool аннотация) - NonNumeric.
/// Fallback на «текст-тип» (literalType) или «сырой» номинал аннотации для НЕвалидного ЗАПРЕЩЁН:
/// тип возвращается только при полностью валидной аннотации, иначе INVALID (ошибку репортит
/// авторитетный проход typeExpr).
enum class LiteralAnnotProblem { None, UnknownType, RationalExpected, BoolNot01, IntOverflow, NonNumeric };

struct LiteralAnnotResult {
    TypeId type = INVALID_TYPE_ID;
    LiteralAnnotProblem problem = LiteralAnnotProblem::None;
};

inline LiteralAnnotResult annotatedLiteralType(const Literal& lit, std::optional<TypeId> ann, const TypeRegistry& reg) {
    if (!ann.has_value() || *ann == INVALID_TYPE_ID) {
        // nullopt - тип не найден (resolveType диагностику НЕ формирует, pass.hpp);
        // INVALID-значение resolveType уже диагностировал сам (ref/template-ошибки).
        return {INVALID_TYPE_ID, LiteralAnnotProblem::UnknownType};
    }
    const TypeId ac = reg.getCanonicalTypeId(*ann);
    const TypeId rat = reg.getCanonicalTypeId(reg.getType(type::Rational));
    // Рациональный литерал `num\den` всегда Rational; аннотация допустима ТОЛЬКО как :Rational.
    if (lit.kind() == ParserToken::Kind::RationalLiteral) {
        return (ac == rat) ? LiteralAnnotResult{*ann, LiteralAnnotProblem::None} : LiteralAnnotResult{INVALID_TYPE_ID, LiteralAnnotProblem::RationalExpected};
    }
    const TypeKind ak = getKindFromId(ac);
    const Group ag = getGroup(ak);
    // BigInteger/Rational: любой целочисленный литерал валиден (в т.ч. сверхразрядный).
    const TypeId bi = reg.getCanonicalTypeId(reg.getType(type::BigInteger));
    if (ac == bi || ac == rat) {
        return {*ann, LiteralAnnotProblem::None};
    }
    // Bool: только 0/1 (снимает неоднозначность 0/1:Bool).
    const TypeId boolT = reg.getCanonicalTypeId(reg.getType(type::Bool));
    if (ac == boolT) {
        const std::string clean = stripDigitSeparators(lit.text());
        return (clean == "0" || clean == "1") ? LiteralAnnotResult{*ann, LiteralAnnotProblem::None}
                                              : LiteralAnnotResult{INVALID_TYPE_ID, LiteralAnnotProblem::BoolNot01};
    }
    // Фиксированные целые/беззнаковые: значение должно влезать в аннотированный тип.
    if (ag == Group::kIntegers || ag == Group::kUnsigned) {
        const bool fits = (lit.kind() == ParserToken::Kind::IntLiteral) && intFitsTarget(lit.text(), ak);
        return fits ? LiteralAnnotResult{*ann, LiteralAnnotProblem::None} : LiteralAnnotResult{INVALID_TYPE_ID, LiteralAnnotProblem::IntOverflow};
    }
    // Float.
    if (ag == Group::kNumbers) {
        return {*ann, LiteralAnnotProblem::None};
    }
    // Не-числовая/не-Bool аннотация на числовом литерале.
    return {INVALID_TYPE_ID, LiteralAnnotProblem::NonNumeric};
}

/// Влезает ли десятичный целочисленный литерал (с '_' и возможным '-') в целевой целый тип
/// (по группе/ширине). Знак-учитывающий: для kIntegers положительный |v| <= 2^(w-1)-1,
/// отрицательный |v| <= 2^(w-1) (= |INTxx_MIN|); для kUnsigned отрицательный не влезает.
/// Единый разбор - parseIntegerLiteral (int_literal.hpp).
inline bool intFitsTarget(std::string_view text, TypeKind targetKind) noexcept {
    const Group g = getGroup(targetKind);
    if (g != Group::kIntegers && g != Group::kUnsigned) {
        return true; // не-целая цель (float) - целочисленный литерал считается безопасным
    }
    const uint8_t width = getData(targetKind);
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
    // kIntegers: знак-учитывающая граница.
    return fitsSignedIntMagnitude(v, width);
}

/// Является ли TypeId универсальным словарём `:Dict` (канонический). Единый предикат для
/// детекции словарного операнда в `[]= ... dict` (spread-merge) - сравнение по каноническому id.
inline bool isDictTypeId(const TypeRegistry& reg, TypeId tid) noexcept {
    if (tid == INVALID_TYPE_ID) {
        return false;
    }
    return reg.getCanonicalTypeId(tid) == reg.getType(type::Dict);
}

/// Является ли TypeId ССЫЛОЧНЫМ (несёт признак ссылки, отличный от kValue). Единый предикат для
/// диагностик контракта value-vs-reference (арифметика над ссылками, swap, копирование ссылки в
/// значение). Составные ссылочные узлы (RefTypeData, вид на pointee) также учитываются - признак
/// берётся из TypeKind самого типа.
inline bool isRefTypeId(TypeId tid) noexcept {
    if (tid == INVALID_TYPE_ID) {
        return false;
    }
    return getRefType(getKindFromId(tid)) != RefType::kValue;
}

/// Является ли имя простым (без сигила/квалификатора) - кандидат на нормализацию `x → $x`
/// (опция -Wsigil) и на «$x-first» резолв. Сигилы: $ локальная, % нативная, @ макро, \\ модуль,
/// : тип, . поле. Квалифицированное (::) имя - не простое.
inline bool isSimpleVarName(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    const char c = name.front();
    if (c == '$' || c == '%' || c == '@' || c == '\\' || c == ':' || c == '.') {
        return false;
    }
    return name.find("::") == std::string_view::npos;
}

/// Человекочитаемое имя ожидаемой категории printf-аргумента (для диагностики).
inline const char* format_expect_name(format_check::Expect expect) noexcept {
    using format_check::Expect;
    switch (expect) {
    case Expect::Integer:
        return "integer";
    case Expect::Unsigned:
        return "unsigned integer";
    case Expect::Float:
        return "floating point";
    case Expect::StrChar:
        return "string";
    case Expect::Pointer:
        return "pointer";
    }
    return "value";
}

/// Имя и значение элемента коллекции из m_body. Элемент - ArgNode (dict/enum/variant) или
/// общий узел (ArrayInit/прочее). Для ArgNode: имя=text(), значение=m_value; иначе элемент сам
/// является значением (позиционный). Единый источник чтения элемента для семантики.
inline void collectionElementNameValue(const AstNodeBase* el, std::string& name, const AstNodeBase*& value) {
    name.clear();
    value = el;
    if (!el) {
        return;
    }
    if (el->kind() == ParserToken::Kind::ArgNode) {
        const auto& a = static_cast<const ArgNode&>(*el);
        name = std::string(a.text());
        value = a.m_value.get();
    }
}

/// Извлечение члена enum/variant из ArgNode: имя, значение (null - безнарный), явный тип.
/// Безнарный член `HIGH` (имя="" и значение-Ident) - имя лежит в значении (Ident), значение
/// отбрасывается (это имя члена, а не значение). Тип члена - напрямую из ArgNode.m_type.
struct EnumVariantMember {
    std::string name;
    AstNodePtr value; // nullptr - безнарный (нет значения)
    AstNodePtr type;  // явный тип (nullptr - нет)
};

inline EnumVariantMember enumVariantMember(const ArgNode& a) {
    EnumVariantMember m;
    m.name = std::string(a.text());
    m.value = a.m_value;
    m.type = a.m_type;
    if (m.name.empty() && m.value && m.value->kind() == ParserToken::Kind::Ident) {
        m.name = std::string(m.value->text());
        m.value = nullptr;
    }
    return m;
}

} // namespace trust
