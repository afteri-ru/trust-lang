// include/ast/ast_helpers.hpp
// Внутренние общие хелперы AST, используемые несколькими категорийными TU (ast_base.cpp,
// ast_expr.cpp, ast_decl.cpp, ast_stmt.cpp, ast_contract.cpp). Вынесены из ast_nodes.cpp,
// чтобы не дублировать при разбиении монолита.
#pragma once

#include "ast/token_base.hpp"
#include "syntax/term.h"
#include <string>
#include <string_view>

namespace trust {

/// Имя узла из Term для операторных термов (VarDecl/FuncDecl/ArgNode):
/// у терма оператора (`:=`, `::=`, ARGUMENT) имя лежит в m_left; иначе - в самом терме.
inline std::string_view declNameFromTerm(const TermPtr& term) {
    if (term->m_left) {
        return term->m_left->getText();
    }
    return term->getText();
}

/// Добавляет строку "label: <child-dump>" с отступом (если child не пуст). Единый формат
/// dump() узлов с детьми. Сохраняет точный прежний вывод:
/// "\n" + indent + "label: " + child->dump(indent+2).
inline void dumpLabeled(std::string& out, size_t indent, std::string_view label, const AstNodePtr& child) {
    if (!child) {
        return;
    }
    out += "\n";
    out += std::string(indent, ' ');
    out += label;
    out += ": ";
    out += child->dump(indent + 2);
}

} // namespace trust
