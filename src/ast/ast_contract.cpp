// src/ast/ast_contract.cpp
// Trust-контракты: TrustContract и термины решателя TrustElem.
// Выделено из ast_nodes.cpp (модуль ast_contract).
#include "ast/ast_nodes.hpp"
#include "ast/ast_helpers.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/trust_prop.hpp"
#include "ast/z3_term.hpp"
#include "syntax/term.h"
#include <string>
namespace trust {

// -- TrustContract: единый узел trust-контракта (pre/post/assert/invariant/type). --
TrustContract::TrustContract(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeBase(k, std::move(term)) {
    if (m_term && m_term->m_left && !m_term->m_left->getText().empty()) {
        // `@{ kind: expr @}`: m_left = идентификатор kind (резолв из фиксированного набора).
        kind = parsePropertyKind(m_term->m_left->getText()).value_or(PropertyKind::kUnknown);
    }
    if (ctx && m_term && m_term->m_right) {
        m_expr = convertChild(*ctx, m_term->m_right);
    }
}

std::string TrustContract::dump(size_t indent) const {
    std::string result = AstNodeBase::dump(indent);
    if (kind != PropertyKind::kUnknown) {
        result += " kind=";
        result += propertyKindName(kind);
    }
    dumpLabeled(result, indent, "expr", m_expr);
    return result;
}

// -- TrustElem: термин решателя `@( term, args... @)` внутри выражения контракта. --
TrustElem::TrustElem(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeBase(k, std::move(term)) {
    if (m_term && m_term->m_left && !m_term->m_left->getText().empty()) {
        kind = parseZ3Term(m_term->m_left->getText()).value_or(Z3TermKind::kUnknown);
    }
    if (ctx && m_term) {
        // Аргументы `@( term, args... @)` хранятся в m_args (ArgsList, из args-терма грамматики).
        if (m_term->m_args) {
            for (const auto& a : *m_term->m_args) {
                if (a.second) {
                    m_args.push_back(convertChild(*ctx, a.second));
                }
            }
        }
    }
}

std::string TrustElem::dump(size_t indent) const {
    std::string result = AstNodeBase::dump(indent);
    result += " term=";
    result += z3TermName(kind);
    for (const auto& a : m_args) {
        result += "\n";
        result += std::string(indent + 1, ' ');
        result += "arg: ";
        result += a ? a->dump(indent + 1) : "<null>";
    }
    return result;
}
} // namespace trust
