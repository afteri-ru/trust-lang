// include/ast/nodes/ast_helpers.hpp
// Внутренние общие хелперы AST, используемые несколькими категорийными TU (ast_base.cpp,
// ast_expr.cpp, ast_decl.cpp, ast_stmt.cpp). Категорийные TU перечислены здесь, чтобы
// общие хелперы не дублировались при разбиении AST по зонам ответственности.
#pragma once

#include "ast/token_base.hpp"
#include "location/location.hpp"
#include "syntax/term/term.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Имя узла из Term для операторных термов (VarDecl/FuncDecl/ArgNode):
/// у терма оператора (`:=`, `::=`, ARGUMENT) имя лежит в m_left; иначе - в самом терме.
/// Возвращает view на время жизни Term: вызывающий обязан скопировать/интернировать.
inline std::string_view declNameFromTerm(const TermPtr& term) {
    if (term->m_left) {
        return term->m_left->getText();
    }
    return term->getText();
}

/// Добавляет строку "label: <child-dump>" с отступом (если child не пуст). Единый формат
/// dump() узлов с детьми: "\n" + indent + "label: " + child->dump(indent+2).
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

/// [min-begin, max-end] по валидным диапазонам детей; nullopt, если объединять нечего.
/// Диапазоны макро-раскрытий из другого псевдо-файла игнорируются (охват - в пределах файла).
[[nodiscard]] inline std::optional<MapperRange> rangeOfNodes(const std::vector<AstNodePtr>& nodes) {
    bool have = false;
    MapperLocation begin;
    MapperLocation end;
    for (const AstNodePtr& node : nodes) {
        if (!node) {
            continue;
        }
        const MapperRange r = node->range();
        if (r.isInvalid()) {
            continue;
        }
        if (!have) {
            begin = r.begin;
            end = r.end;
            have = true;
            continue;
        }
        if (r.begin.fileIdx() != begin.fileIdx()) {
            continue; // макро-раскрытие в другом псевдо-файле
        }
        if (r.begin.offset() < begin.offset()) {
            begin = r.begin;
        }
        if (r.end.offset() > end.offset()) {
            end = r.end;
        }
    }
    if (have && begin.offset() <= end.offset()) {
        return MapperRange{begin, end};
    }
    return std::nullopt;
}

} // namespace trust
