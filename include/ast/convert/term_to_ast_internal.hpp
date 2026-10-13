#pragma once
// include/ast/convert/term_to_ast_internal.hpp
// Shared helpers used by several Term->AST conversion TUs.

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"

#include <vector>

namespace trust {

class Context;

namespace detail {

/// Переносит атрибуты `@[...]` терма в узел (единая точка для всех конвертеров).
void convertAttrsToNode(const TermPtr& term, AstNodePtr& node, Context& ctx);

/// Есть ли у терма конвертируемые дети (TRUST_* пропускаются).
bool hasConvertibleChildren(const TermPtr& term);

} // namespace detail

// -- Свободные хелперы конвертации (определены в term_to_ast.cpp) --
AstNodePtr convertChild(Context& ctx, const TermPtr& term);
void convertChildren(Context& ctx, const TermPtr& term, std::vector<AstNodePtr>& out);
void convertModuleBody(Context& ctx, const TermPtr& term, std::vector<AstNodePtr>& out);
void appendDictElementsFromArgs(Context& ctx, const TermPtr& term, DictLiteralNode& node);
bool argIsTypeForm(const TermPtr& argTerm);
bool isAllTupleTypeArgs(const TermPtr& term);
AstNodePtr buildStringFormatCall(ParserToken::Kind litKind, const TermPtr& term, Context& ctx);

} // namespace trust
