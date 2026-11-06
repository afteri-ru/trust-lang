#pragma once
// Shared helpers used by several Term->AST conversion TUs.

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"

#include <vector>

namespace trust {

struct ConvertServices;

namespace detail {

/// Переносит атрибуты `@[...]` терма в узел (единая точка для всех конвертеров).
void convertAttrsToNode(const TermPtr& term, AstNodePtr& node, ConvertServices& ctx);

/// Есть ли у терма конвертируемые дети (TRUST_* пропускаются).
bool hasConvertibleChildren(const TermPtr& term);

} // namespace detail

AstNodePtr convertChild(ConvertServices& ctx, const TermPtr& term);
void convertChildren(ConvertServices& ctx, const TermPtr& term, std::vector<AstNodePtr>& out);
void convertModuleBody(ConvertServices& ctx, const TermPtr& term, std::vector<AstNodePtr>& out);
void appendDictElementsFromArgs(ConvertServices& ctx, const TermPtr& term, DictLiteralNode& node);
bool argIsTypeForm(const TermPtr& argTerm);
bool isAllTupleTypeArgs(const TermPtr& term);
AstNodePtr buildStringFormatCall(ParserToken::Kind litKind, const TermPtr& term, ConvertServices& ctx);

} // namespace trust
