// src/ast/ast_expr.cpp
// Выражения AST: Literal, ContextMacro, Binary, CallExpr, RangeExpr.
// Выделено из ast_nodes.cpp (модуль ast_expr).
#include "ast/ast_nodes.hpp"
#include "ast/ast_helpers.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "syntax/term.h"
#include "utils/error.hpp"
#include <string>
namespace trust {

Binary::Binary(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "Binary term-constructor requires a source Term");
    if (ctx) {
        if (m_term->m_left) {
            m_left = convertChild(*ctx, m_term->m_left);
        }
        if (m_term->m_right) {
            m_right = convertChild(*ctx, m_term->m_right);
        }
    }
}

CallExpr::CallExpr(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "CallExpr term-constructor requires a source Term");
    if (ctx) {
        m_callee = std::make_shared<IdentName>(m_term);
        std::vector<AstNodePtr> args;
        convertChildren(*ctx, m_term, args);
        if (!args.empty()) {
            m_args = std::move(args);
        }
    }
}

// -- Literal::dump --

std::string Literal::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    if (kind() == ParserToken::Kind::StrChar || kind() == ParserToken::Kind::StrWide) {
        result += " \"";
        result += text();
        result += "\"";
    } else {
        // Numeric and other non-string literals are printed without quotes
        // (matching the convention used by clang -ast-dump).
        result += " ";
        result += text();
    }
    return result;
}

// -- ContextMacro::dump --

std::string ContextMacro::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    if (!text().empty()) {
        result += " '";
        result += text();
        result += "'";
    }
    return result;
}

// -- Binary::dump --

std::string Binary::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "left", m_left);
    dumpLabeled(result, indent, "right", m_right);
    return result;
}

// -- CallExpr::dump --

std::string CallExpr::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "callee", m_callee);
    if (m_args) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "args:";
        for (size_t i = 0; i < m_args->size(); i++) {
            const auto& arg = (*m_args)[i];
            result += "\n";
            if (arg) {
                result += arg->dump(indent + 2);
            } else {
                result += std::string(indent + 2, ' ') + "(null)";
            }
        }
    }
    return result;
}

// -- RangeExpr::dump --

std::string RangeExpr::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (elementType != INVALID_TYPE_ID) {
        result += "elem=";
        result += std::to_string(elementType);
        result += ' ';
    }
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

MapperRange Binary::range() const {
    if (!m_term) {
        return {};
    }
    // [left.begin, right.end] - обе стороны обязательны (операторный терм несёт range только
    // оператора). Совпадает с прежним expandTermRangeToChildren, но охват читается из детей.
    if (m_left && m_right) {
        const MapperRange l = m_left->range();
        const MapperRange r = m_right->range();
        if (!l.isInvalid() && !r.isInvalid() && l.begin.fileIdx() == r.end.fileIdx() && l.begin.offset() <= r.end.offset()) {
            return MapperRange{l.begin, r.end};
        }
    }
    return AstNodeAttr::range();
}
} // namespace trust
