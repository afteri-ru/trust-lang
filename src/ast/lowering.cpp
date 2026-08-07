// src/ast/lowering.cpp
// Реализация понижения: каждый класс узла переопределяет AstNodeBase::lower(self, ctx)
// согласно своему Kind и понижает своих детей. Свободные функции lowerBody/lowerBodyNode/
// lowerNode — векторно-ориентированные помощники (оборачивают statement-выражения в SemicolonStmt,
// вставляют continue-метки перед первым циклом именованного блока, рекурсивно понижают детей).

#include "ast/lowering.hpp"

#include "syntax/term.h"

#include <string>
#include <vector>

namespace trust {

// ── Вспомогательные ──

std::string cleanLabelName(std::string_view name) {
    std::string clean;
    clean.reserve(name.size());
    for (char c : name) {
        if (c != ':') {
            clean += c;
        }
    }
    return clean;
}

std::string funcNameOf(const FuncDecl* fd) {
    std::string s{fd->text()};
    if (!s.empty() && s[0] == '%') {
        s.erase(0, 1);
    }
    return cleanLabelName(s);
}

bool isExprStatement(ParserToken::Kind k) noexcept {
    switch (k) {
    case ParserToken::Kind::NameDecl:
    case ParserToken::Kind::AssignOp:
    case ParserToken::Kind::AppendStmt:
    case ParserToken::Kind::MathOp:
    case ParserToken::Kind::BitwiseOp:
    case ParserToken::Kind::CompareOp:
    case ParserToken::Kind::LogicalOp:
    case ParserToken::Kind::MemberAccess:
    case ParserToken::Kind::ArrayAccess:
    case ParserToken::Kind::CallExpr:
    case ParserToken::Kind::IntLiteral:
    case ParserToken::Kind::FloatLiteral:
    case ParserToken::Kind::StrChar:
    case ParserToken::Kind::StrWide:
    case ParserToken::Kind::RationalLiteral:
        return true;
    default:
        return false;
    }
}

void appendLabel(AstNodePtr& bodyNode, const std::string& label) {
    if (!bodyNode) {
        return;
    }
    if (is_block_kind(bodyNode->kind())) {
        auto* seq = bodyNode->as_sequence();
        seq->m_body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, label));
    } else {
        auto seq = std::make_shared<Sequence>(ParserToken::Kind::sequence, std::string{});
        seq->m_body.push_back(std::move(bodyNode));
        seq->m_body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, label));
        bodyNode = std::move(seq);
    }
}

// ── Векторные помощники ──

void lowerNode(AstNodePtr& node, LowerCtx& ctx) {
    if (node) {
        node->lower(node, ctx);
    }
}

void lowerBody(std::vector<AstNodePtr>& body, LowerCtx& ctx) {
    std::vector<AstNodePtr> out;
    out.reserve(body.size() * 2 + 1);
    for (auto& child : body) {
        if (!child) {
            continue;
        }
        // continue-метка именованного блока: первый цикл в теле (по DFS) потребляет её.
        if (!ctx.pendingContinue.empty()) {
            if (child->kind() == ParserToken::Kind::WhileStmt) {
                // while: метка перед циклом — goto переоценивает условие, не повторяя init.
                out.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, ctx.pendingContinue));
                ctx.pendingContinue.clear();
            } else if (child->kind() == ParserToken::Kind::DoWhileStmt) {
                // do-while: метка в КОНЕЦ тела (перед '}') — goto переходит к проверке условия.
                appendLabel(static_cast<DoWhileStmt*>(child.get())->m_body, ctx.pendingContinue);
                ctx.pendingContinue.clear();
            }
        }
        // SemicolonStmt-обёртка: точка с запятой для statement-выражений явно в AST.
        if (isExprStatement(child->kind())) {
            out.push_back(std::make_shared<SemicolonStmt>(std::move(child)));
        } else {
            out.push_back(std::move(child));
        }
    }
    body = std::move(out);
    // Рекурсивно вниз (после вставки обёрток, чтобы pendingContinue учитывал новые узлы).
    for (auto& child : body) {
        if (child) {
            lowerNode(child, ctx);
        }
    }
}

void lowerBodyNode(AstNodePtr& bodyNode, LowerCtx& ctx) {
    if (!bodyNode) {
        return;
    }
    // Тело цикла/ветки — НЕ именованный блок: если это блок (Sequence/ScopeBlock/ModuleNode),
    // обрабатываем его m_body как операторы (без named-block меток); иначе — одиночный оператор.
    if (is_block_kind(bodyNode->kind())) {
        lowerBody(static_cast<Sequence*>(bodyNode.get())->m_body, ctx);
    } else {
        std::vector<AstNodePtr> one;
        one.push_back(std::move(bodyNode));
        lowerBody(one, ctx);
        bodyNode = std::move(one[0]);
    }
}

// ── Node lower: понижение согласно Kind каждого класса ──

void AstNodeBase::lower(AstNodePtr&, LowerCtx&) {
}

void Sequence::lower(AstNodePtr&, LowerCtx& ctx) {
    // DictLiteral / Tuple / RangeExpr — это Sequence структурно, но их m_body — это операнды
    // (у кортежа/конструкции: [имя=значение, ...]; у диапазона: [start, stop, (step)]),
    // а НЕ список операторов. Поэтому НЕ оборачиваем детей в SemicolonStmt (иначе каждый
    // элемент получил бы ';'), а понижаем их рекурсивно как выражения.
    if (m_kind == ParserToken::Kind::DictLiteral || m_kind == ParserToken::Kind::Tuple || m_kind == ParserToken::Kind::RangeExpr) {
        for (auto& child : m_body) {
            lowerNode(child, ctx);
        }
        return;
    }
    lowerBody(m_body, ctx);
}

void ScopeBlock::lower(AstNodePtr&, LowerCtx& ctx) {
    if (ctx.inFunction && !is_anonymous() && !is_hidden()) {
        const std::string clean = cleanLabelName(name());
        if (!clean.empty()) {
            // Метки именованного блока: break — после тела, continue — первый цикл в теле.
            const std::string saved = ctx.pendingContinue;
            ctx.pendingContinue = clean + "_continue";
            lowerBody(m_body, ctx);
            m_body.push_back(std::make_shared<LabelRef>(ParserToken::Kind::LabelStmt, clean + "_break"));
            ctx.pendingContinue = saved;
            return;
        }
    }
    lowerBody(m_body, ctx);
}

void ModuleNode::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerBody(m_body, ctx);
}

void FuncDecl::lower(AstNodePtr&, LowerCtx& ctx) {
    // Тело функции — вектор операторов (m_body), range блока берётся из m_term->m_right.
    if (m_body) {
        const bool savedInFn = ctx.inFunction;
        const std::string savedFunc = ctx.funcName;
        ctx.inFunction = true;
        ctx.funcName = funcNameOf(this);
        lowerBody(*m_body, ctx);
        ctx.inFunction = savedInFn;
        ctx.funcName = savedFunc;
    }
}

void IfStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_cond, ctx);
    lowerBodyNode(m_body, ctx);
    for (auto& [c, b] : m_elseifs) {
        lowerNode(c, ctx);
        lowerBodyNode(b, ctx);
    }
    lowerBodyNode(m_else, ctx);
}

void WhileStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_cond, ctx);
    lowerBodyNode(m_body, ctx);
    lowerBodyNode(m_else, ctx);
}

void DoWhileStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerBodyNode(m_body, ctx);
    lowerNode(m_cond, ctx);
}

void MatchStmt::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_value, ctx);
    for (auto& c : m_cases) {
        for (auto& p : c.patterns) {
            lowerNode(p, ctx);
        }
        lowerBodyNode(c.body, ctx);
    }
    lowerBodyNode(m_default, ctx);
}

void JumpStmt::lower(AstNodePtr& self, LowerCtx& ctx) {
    // Именованные break/continue → goto; break по имени функции → void-return.
    if (kind() == ParserToken::Kind::BreakStmt || kind() == ParserToken::Kind::ContinueStmt) {
        if (m_label) {
            const std::string clean = cleanLabelName(m_label->text());
            if (kind() == ParserToken::Kind::BreakStmt) {
                if (ctx.inFunction && !ctx.funcName.empty() && clean == ctx.funcName) {
                    // Синтетический узел без Term (невалидный range, без source-map).
                    self = std::make_shared<JumpStmt>(ParserToken::Kind::ReturnStmt);
                } else {
                    self = std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, clean + "_break");
                }
            } else {
                self = std::make_shared<LabelRef>(ParserToken::Kind::GotoStmt, clean + "_continue");
            }
        }
        return;
    }
    // return/throw: понижаем значение.
    lowerNode(m_value, ctx);
}

void VarDecl::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_initializer, ctx);
    lowerNode(m_type, ctx);
}

void Binary::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_left, ctx);
    lowerNode(m_right, ctx);
}

void CallExpr::lower(AstNodePtr&, LowerCtx& ctx) {
    lowerNode(m_callee, ctx);
    if (m_args) {
        for (auto& a : *m_args) {
            lowerNode(a, ctx);
        }
    }
}

} // namespace trust
