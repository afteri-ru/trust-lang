#pragma once

#include "ast/token.hpp"
#include "diag/context.hpp"

#include <memory>
#include <vector>

namespace trust {
class Term;
using TermPtr = std::shared_ptr<Term>;
} // namespace trust

namespace trust {

/// Convert a legacy TermPtr tree into a vector of AstNodePtr.
/// Each Term is recursively traversed (m_left, m_right, m_block, m_args)
/// and converted to an AstNodeBase with the closest ParserToken::Kind mapping.
/// @param term  Root of legacy AST tree
/// @param ctx   Diagnostic context (for error reporting)
/// @return      Flat vector of AST nodes
std::vector<AstNodePtr> termToAst(const trust::TermPtr& term, Context& ctx);

} // namespace trust