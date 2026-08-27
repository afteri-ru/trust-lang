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

#include <string>
#include <string_view>

namespace trust {

/// Влезает ли десятичный целочисленный литерал в целевой целый тип (по группе/ширине).
/// Границы - единый источник `fitsIntegerValue` (type_inference.hpp).
inline bool intFitsTarget(std::string_view text, TypeKind targetKind) noexcept {
    const Group g = getGroup(targetKind);
    if (g != Group::kIntegers && g != Group::kUnsigned) {
        return true; // не-целая цель (float) - целочисленный литерал считается безопасным
    }
    unsigned long long v = 0;
    if (!parseDecimalUInt(text, v)) {
        return false; // отрицательный/нецелой литерал не типизируем как положительный
    }
    return fitsIntegerValue(g, getData(targetKind), v);
}

/// Является ли TypeId универсальным словарём `:Dict` (канонический). Единый предикат для
/// детекции словарного операнда в `[]= ... dict` (spread-merge) - сравнение по каноническому id.
inline bool isDictTypeId(const TypeRegistry& reg, TypeId tid) noexcept {
    if (tid == INVALID_TYPE_ID) {
        return false;
    }
    return reg.getCanonicalTypeId(tid) == reg.getType(type::Dict);
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
