// ast_nodes.cpp — реализации Binary, CallExpr, Scope, Decl, JumpStmt, VarDecl
// + утилита dumpBody() для устранения дублирования обхода std::vector<AstNodePtr>

#include "ast/ast_nodes.hpp"
#include "syntax/term.h"

namespace trust {

// ── Sequence::dumpBody — дамп содержимого std::vector<AstNodePtr> ──

void Sequence::dumpBody(std::string& result, const std::vector<AstNodePtr>& body, size_t indent, size_t child_indent) {
    if (body.empty())
        return;
    result += "\n";
    for (size_t i = 0; i < body.size(); i++) {
        result += std::string(indent, ' ');
        if (body[i])
            result += body[i]->dump(child_indent);
        if (i + 1 < body.size()) {
            result += "\n";
        }
    }
}

// ── Literal::dump ──

std::string Literal::dump(size_t indent) const {
    std::string result = std::string(indent, ' ');
    result += ParserToken::name(kind());
    if (kind() == ParserToken::Kind::StringLiteral) {
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

// ── Binary::dump ──

std::string Binary::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (m_left) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "left: ";
        result += m_left->dump(indent + 2);
    }
    if (m_right) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "right: ";
        result += m_right->dump(indent + 2);
    }
    return result;
}

// ── CallExpr::dump ──

std::string CallExpr::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (m_callee) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "callee: ";
        result += m_callee->dump(indent + 2);
    }
    if (m_args) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "args:";
        for (size_t i = 0; i < m_args->size(); i++) {
            const auto& arg = (*m_args)[i];
            result += "\n";
            if (arg)
                result += arg->dump(indent + 2);
            else
                result += std::string(indent + 2, ' ') + "(null)";
        }
    }
    return result;
}

// ── ParamDecl::dump ──

std::string ParamDecl::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    result += " '";
    result += text();
    result += "'";
    if (m_type) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "type: ";
        result += m_type->dump(indent + 2);
    }
    if (m_default) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "default: ";
        result += m_default->dump(indent + 2);
    }
    return result;
}

// ── Sequence::dump ──

std::string Sequence::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// ── ScopeBlock::dump ──

std::string ScopeBlock::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (!text().empty()) {
        result += text();
    } else if (m_blockCounter > 0) {
        result += std::to_string(m_blockCounter);
    }
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// ── ModuleNode::dump ──

std::string ModuleNode::dump(size_t indent) const {
    std::string result = Sequence::dump(indent);
    result += " '";
    result += text();
    result += "'";
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// ── Decl::dump ──

std::string Decl::dump(size_t indent) const {
    std::string result = IdentName::dump(indent);
    result += " '";
    result += text();
    result += "'";
    if (m_type) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "type: ";
        result += m_type->dump(indent + 2);
    }
    return result;
}

// ── FuncDecl::dump ──

std::string FuncDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (m_params) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "params:";
        for (size_t i = 0; i < m_params->size(); ++i) {
            result += "\n";
            const auto& param = (*m_params)[i];
            if (param)
                result += param->dump(indent + 2);
            else
                result += std::string(indent + 2, ' ') + "(null)";
        }
    }
    if (m_body) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "body:";
        for (size_t i = 0; i < m_body->size(); ++i) {
            result += "\n";
            const auto& stmt = (*m_body)[i];
            if (stmt)
                result += stmt->dump(indent + 2);
            else
                result += std::string(indent + 2, ' ') + "(null)";
        }
    } else {
        result += " (forward)";
    }
    return result;
}

MapperRange FuncDecl::blockRange() const noexcept {
    // Тело функции — блок { ... } лежит в m_term->m_right (терм CREATE_TYPE ::=).
    // Его range используется для определения строк { и }, чтобы сгенерированный код
    // повторял раскладку исходника. Для test-only узлов без терма — invalid range.
    if (m_term && m_term->m_right)
        return m_term->m_right->m_mapperRange;
    return {};
}

// ── JumpStmt::dump ──

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

// ── VarDecl::dump ──

std::string VarDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (m_initializer) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "init: ";
        result += m_initializer->dump(indent + 2);
    }
    if (m_is_mutable)
        result += " mut";
    return result;
}

// ── VarDecl::nameRange ──
// Диапазон реального имени. Базовый m_term для `x := ...` — терм оператора ':=':
// его m_mapperRange указывает на оператор, а имя лежит в m_term->m_left.
MapperRange VarDecl::nameRange() const noexcept {
    if (m_term && m_term->m_left && !m_term->m_left->m_mapperRange.isInvalid())
        return m_term->m_left->m_mapperRange;
    return {};
}

// ── IfStmt::dump ──

std::string IfStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (m_cond)
        result += "\n" + std::string(indent, ' ') + "cond: " + m_cond->dump(indent + 2);
    if (m_then)
        result += "\n" + std::string(indent, ' ') + "then: " + m_then->dump(indent + 2);
    for (size_t i = 0; i < m_elseifs.size(); ++i) {
        result += "\n" + std::string(indent, ' ') + "elseif" + std::to_string(i) + ":";
        if (m_elseifs[i].first)
            result += "\n" + std::string(indent + 2, ' ') + "cond: " + m_elseifs[i].first->dump(indent + 4);
        if (m_elseifs[i].second)
            result += "\n" + std::string(indent + 2, ' ') + "body: " + m_elseifs[i].second->dump(indent + 4);
    }
    if (m_else)
        result += "\n" + std::string(indent, ' ') + "else: " + m_else->dump(indent + 2);
    return result;
}

// ── WhileStmt::dump ──

std::string WhileStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (m_cond)
        result += "\n" + std::string(indent, ' ') + "cond: " + m_cond->dump(indent + 2);
    if (m_body)
        result += "\n" + std::string(indent, ' ') + "body: " + m_body->dump(indent + 2);
    if (m_else)
        result += "\n" + std::string(indent, ' ') + "else: " + m_else->dump(indent + 2);
    return result;
}

// ── DoWhileStmt::dump ──

std::string DoWhileStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (m_body)
        result += "\n" + std::string(indent, ' ') + "body: " + m_body->dump(indent + 2);
    if (m_cond)
        result += "\n" + std::string(indent, ' ') + "cond: " + m_cond->dump(indent + 2);
    return result;
}

// ── MatchStmt::dump ──

std::string MatchStmt::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (m_value)
        result += "\n" + std::string(indent, ' ') + "value: " + m_value->dump(indent + 2);
    for (size_t i = 0; i < m_cases.size(); ++i) {
        result += "\n" + std::string(indent, ' ') + "case" + std::to_string(i) + ":";
        for (size_t j = 0; j < m_cases[i].patterns.size(); ++j)
            result += "\n" + std::string(indent + 2, ' ') + "pat" + std::to_string(j) + ": " + m_cases[i].patterns[j]->dump(indent + 4);
        if (m_cases[i].body)
            result += "\n" + std::string(indent + 2, ' ') + "body: " + m_cases[i].body->dump(indent + 4);
    }
    if (m_default)
        result += "\n" + std::string(indent, ' ') + "default: " + m_default->dump(indent + 2);
    return result;
}

} // namespace trust