// src/ast/ast_stmt.cpp
// Операторы AST: JumpStmt, ControlFlowStmt/IfStmt/WhileStmt/DoWhileStmt, MatchStmt,
// LabelRef, SemicolonStmt. Выделено из ast_nodes.cpp (модуль ast_stmt).
#include "ast/ast_nodes.hpp"
#include "ast/ast_helpers.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "syntax/term.h"
#include "utils/error.hpp"
#include <optional>
#include <string>
#include <vector>
namespace trust {

JumpStmt::JumpStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "JumpStmt term-constructor requires a source Term");

    // Дети нужны для роли (break vs return) и значения.
    std::vector<AstNodePtr> body;
    if (ctx) {
        convertChildren(*ctx, m_term, body);
    }

    const auto id = m_term->getTermID();
    // Роль определяется по TermID и наличию значения:
    //   INT_PLUS  (++) без значения → break; со значением → return
    //   INT_REPEAT (-+ / +-)        → continue
    //   INT_MINUS (--)              → throw
    if (id == trust::TermID::INT_PLUS || id == trust::TermID::INT_MINUS || id == trust::TermID::INT_REPEAT) {
        if (id == trust::TermID::INT_MINUS) {
            m_kind = ParserToken::Kind::ThrowStmt;
        } else if (id == trust::TermID::INT_REPEAT) {
            m_kind = ParserToken::Kind::ContinueStmt;
        } else { // INT_PLUS
            m_kind = body.empty() ? ParserToken::Kind::BreakStmt : ParserToken::Kind::ReturnStmt;
        }

        // Текст узла AST - '++'/'--'/'-+'; namespace (label) из m_text уходит в m_label.
        std::string_view ns = m_term->getText();
        const char* op = (m_kind == ParserToken::Kind::ReturnStmt || m_kind == ParserToken::Kind::BreakStmt) ? "++"
                         : (m_kind == ParserToken::Kind::ThrowStmt)                                          ? "--"
                                                                                                             : "-+";
        if (!ns.empty() && ns != op) {
            auto labelTerm = Term::Create(trust::TermID::NAME, std::string(ns));
            m_label = std::make_shared<AstNodeAttr>(ParserToken::Kind::Ident, std::move(labelTerm));
        }
    }

    if (!body.empty() && m_kind != ParserToken::Kind::BreakStmt && m_kind != ParserToken::Kind::ContinueStmt) {
        // void return `++ _ ++`: значение `_` - служебный символ, m_value = nullptr.
        if (m_kind == ParserToken::Kind::ReturnStmt && body[0] && body[0]->kind() == ParserToken::Kind::Ident && body[0]->text() == "_") {
            m_value = nullptr;
        } else {
            m_value = std::move(body[0]);
        }
    }
}

IfStmt::IfStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: ControlFlowStmt(k, std::move(term)) {
    EXPECT(m_term && "IfStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // Единая раскладка: m_left=условие, m_right=else,
    // m_sequence=[тело then, branch2, branch3, ...] (branch_i = терм cond_i→body_i).
    if (m_term->m_left) {
        m_cond = convertChild(*ctx, m_term->m_left);
    }
    if (!m_term->m_sequence.empty() && m_term->m_sequence[0]) {
        m_body = convertChild(*ctx, m_term->m_sequence[0]);
    }
    for (size_t i = 1; i < m_term->m_sequence.size(); ++i) {
        const auto& br = m_term->m_sequence[i];
        if (!br) {
            continue;
        }
        AstNodePtr c = br->m_left ? convertChild(*ctx, br->m_left) : nullptr;
        AstNodePtr b = br->m_right ? convertChild(*ctx, br->m_right) : nullptr;
        m_elseifs.emplace_back(std::move(c), std::move(b));
    }
    if (m_term->m_right) {
        m_else = convertChild(*ctx, m_term->m_right);
    }
}

WhileStmt::WhileStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: ControlFlowStmt(k, std::move(term)) {
    EXPECT(m_term && "WhileStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // m_left=cond, m_sequence=[body], m_right=else.
    if (m_term->m_left) {
        m_cond = convertChild(*ctx, m_term->m_left);
    }
    if (!m_term->m_sequence.empty() && m_term->m_sequence[0]) {
        m_body = convertChild(*ctx, m_term->m_sequence[0]);
    }
    if (m_term->m_right) {
        m_else = convertChild(*ctx, m_term->m_right);
    }
}

DoWhileStmt::DoWhileStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: ControlFlowStmt(k, std::move(term)) {
    EXPECT(m_term && "DoWhileStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // do-while: m_left=cond, m_sequence=[body].
    if (!m_term->m_sequence.empty() && m_term->m_sequence[0]) {
        m_body = convertChild(*ctx, m_term->m_sequence[0]);
    }
    if (m_term->m_left) {
        m_cond = convertChild(*ctx, m_term->m_left);
    }
}

MatchStmt::MatchStmt(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, std::move(term)) {
    EXPECT(m_term && "MatchStmt term-constructor requires a source Term");
    if (!ctx) {
        return;
    }
    // Раскладка MATCHING-терма: m_left=значение, m_right=match_body (BLOCK);
    // m_right->m_sequence = [item1, ..., elseItem]; item: m_left=шаблоны (m_sequence=list),
    // m_right=тело; else: m_left=ELLIPSIS.
    if (m_term->m_left) {
        m_value = convertChild(*ctx, m_term->m_left);
    }
    if (m_term->m_right) {
        for (const auto& item : m_term->m_right->m_sequence) {
            if (!item) {
                continue;
            }
            if (item->m_left && item->m_left->getTermID() == trust::TermID::ELLIPSIS) {
                // else: [...] --> body
                m_default = item->m_right ? convertChild(*ctx, item->m_right) : nullptr;
            } else {
                MatchCase c;
                if (item->m_left) {
                    // matches: первый шаблон - сам терм, остальные - в его m_sequence
                    c.patterns.push_back(convertChild(*ctx, item->m_left));
                    for (const auto& p : item->m_left->m_sequence) {
                        c.patterns.push_back(convertChild(*ctx, p));
                    }
                }
                c.body = item->m_right ? convertChild(*ctx, item->m_right) : nullptr;
                m_cases.push_back(std::move(c));
            }
        }
    }
}

// -- JumpStmt::dump --

std::string JumpStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (m_label) {
        result += " label:";
        result += m_label->text();
    }
    if (m_value) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "value: ";
        result += m_value->dump(indent + 2);
    } else {
        result += " void";
    }
    return result;
}

// -- ControlFlowStmt::dumpControlFlow --
std::string ControlFlowStmt::dumpControlFlow(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "cond", m_cond);
    dumpLabeled(result, indent, "body", m_body);
    dumpLabeled(result, indent, "else", m_else);
    return result;
}

// -- IfStmt::dump --

std::string IfStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "cond", m_cond);
    dumpLabeled(result, indent, "then", m_body);
    for (size_t i = 0; i < m_elseifs.size(); ++i) {
        result += "\n" + std::string(indent, ' ') + "elseif" + std::to_string(i) + ":";
        if (m_elseifs[i].first) {
            result += "\n" + std::string(indent + 2, ' ') + "cond: " + m_elseifs[i].first->dump(indent + 4);
        }
        if (m_elseifs[i].second) {
            result += "\n" + std::string(indent + 2, ' ') + "body: " + m_elseifs[i].second->dump(indent + 4);
        }
    }
    dumpLabeled(result, indent, "else", m_else);
    return result;
}

// -- WhileStmt::dump --

std::string WhileStmt::dump(size_t indent) const {
    return dumpControlFlow(indent);
}

// -- DoWhileStmt::dump --
std::string DoWhileStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "body", m_body);
    dumpLabeled(result, indent, "cond", m_cond);
    return result;
}

// -- MatchStmt::dump --

std::string MatchStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "value", m_value);
    for (size_t i = 0; i < m_cases.size(); ++i) {
        result += "\n" + std::string(indent, ' ') + "case" + std::to_string(i) + ":";
        for (size_t j = 0; j < m_cases[i].patterns.size(); ++j) {
            result += "\n" + std::string(indent + 2, ' ') + "pat" + std::to_string(j) + ": " + m_cases[i].patterns[j]->dump(indent + 4);
        }
        if (m_cases[i].body) {
            result += "\n" + std::string(indent + 2, ' ') + "body: " + m_cases[i].body->dump(indent + 4);
        }
    }
    dumpLabeled(result, indent, "default", m_default);
    return result;
}

// -- LabelRef (kind=GotoStmt|LabelStmt) / SemicolonStmt: dump --
std::string LabelRef::dump(size_t indent) const {
    return detail::dumpQuotedName(kind(), m_name, indent);
}

std::string SemicolonStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpLabeled(result, indent, "expr", m_expr);
    return result;
}

// -- Диапазоны узлов: вычисляются на лету по узлам-детям, исходный Term НЕ мутируется. --
// [min-begin, max-end] по ранжам узлов-детей (guard: только дети из того же файла, что и первый).
static std::optional<MapperRange> spanOfNodes(const std::vector<AstNodePtr>& children) {
    bool have = false;
    trust::MapperLocation begin, end;
    auto consider = [&](const MapperRange& r) {
        if (r.isInvalid()) {
            return;
        }
        if (!have) {
            begin = r.begin;
            end = r.end;
            have = true;
            return;
        }
        if (r.begin.fileIdx() != begin.fileIdx()) {
            return; // макро-раскрытие: ребёнок в другом псевдо-файле - пропускаем
        }
        if (r.begin.offset() < begin.offset()) {
            begin = r.begin;
        }
        if (r.end.offset() > end.offset()) {
            end = r.end;
        }
    };
    for (const auto& c : children) {
        if (c) {
            consider(c->range());
        }
    }
    if (have && begin.offset() <= end.offset()) {
        return trust::MapperRange{begin, end};
    }
    return std::nullopt;
}

MapperRange ControlFlowStmt::range() const {
    if (!m_term) {
        return {};
    }
    if (auto r = spanOfNodes({m_cond, m_body, m_else})) {
        return *r;
    }
    return AstNodeAttr::range();
}

MapperRange IfStmt::range() const {
    if (!m_term) {
        return {};
    }
    std::vector<AstNodePtr> children;
    children.reserve(2 + m_elseifs.size() * 2 + 1);
    children.push_back(m_cond);
    children.push_back(m_body);
    for (const auto& [cond, body] : m_elseifs) {
        children.push_back(cond);
        children.push_back(body);
    }
    children.push_back(m_else);
    if (auto r = spanOfNodes(children)) {
        return *r;
    }
    return AstNodeAttr::range();
}

MapperRange MatchStmt::range() const {
    if (!m_term) {
        return {};
    }
    std::vector<AstNodePtr> children;
    children.push_back(m_value);
    for (const auto& c : m_cases) {
        for (const auto& p : c.patterns) {
            children.push_back(p);
        }
        children.push_back(c.body);
    }
    children.push_back(m_default);
    if (auto r = spanOfNodes(children)) {
        return *r;
    }
    return AstNodeAttr::range();
}
} // namespace trust
