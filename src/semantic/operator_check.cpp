// src/semantic/operator_check.cpp
// Валидация объявления перегружаемого оператора (см. semantic/operator_check.hpp).
// Символ оператора - текст в обратных кавычках (лексема REFLECTION); допустимый набор и
// правила member/free/арности - ЕДИНЫЙ источник types/operator_registry.hpp.
#include "semantic/operator_check.hpp"
#include "diag/diag.hpp"
#include "diag/registry.hpp"
#include "types/operator_registry.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace trust::semantic {

namespace {

// Ожидаемое число ПАРАМЕТРОВ объявления. op::Arity считает ОПЕРАНДЫ: member-форма имеет
// неявный *this (на один параметр меньше), free-форма - все операнды. nullopt - не фиксировано.
std::optional<size_t> expectedParamCount(op::Arity arity, bool isMember) {
    switch (arity) {
    case op::Arity::kUnary:
        return isMember ? 0u : 1u;
    case op::Arity::kBinary:
        return isMember ? 1u : 2u;
    case op::Arity::kIndex:
        return 1u; // всегда member, ровно один индекс
    case op::Arity::kCall:
        return std::nullopt; // operator() - произвольное число аргументов
    }
    return std::nullopt;
}

} // namespace

bool validateOperatorDecl(Context& ctx, const FuncDecl& f, bool isMember) {
    const std::string_view sym = f.text();
    // Оператор-ШАБЛОН (<T> `==`(...)) не реализован: явная ошибка (не молчаливый пропуск).
    if (f.m_templateParams.has_value()) {
        ctx.diag().report(Severity::Error, f.range(), "template operators are not implemented");
        return false;
    }
    const op::OperatorInfo* info = op::findOperator(sym);
    if (info == nullptr) {
        ctx.diag().report(Severity::Error, f.range(), "operator '{}' is not implemented", sym);
        return false;
    }
    if (isMember && !info->allowMember) {
        ctx.diag().report(Severity::Error, f.range(), "operator '{}' cannot be declared as a class method", sym);
        return false;
    }
    if (!isMember && !info->allowFree) {
        ctx.diag().report(Severity::Error, f.range(), "operator '{}' must be declared as a class method", sym);
        return false;
    }
    if (!f.m_type) {
        ctx.diag().report(Severity::Error, f.range(), "operator '{}' must have an explicit return type", sym);
        return false;
    }
    const size_t params = (f.m_params ? f.m_params->size() : 0);
    if (const std::optional<size_t> expected = expectedParamCount(info->arity, isMember); expected.has_value() && params != *expected) {
        ctx.diag().report(Severity::Error, f.range(), "operator '{}' in this form expects {} parameter(s), got {}", sym, *expected, params);
        return false;
    }
    return true;
}

} // namespace trust::semantic
