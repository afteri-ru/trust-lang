// attr_parser.hpp — parse @[...] attribute tokens into AttrId
//
// Given a sequence of tokens between @[...] and ]@, this parser:
// 1. Extracts the attribute name (first NAME token)
// 2. Extracts parameters from the optional (...) section
// 3. Validates parameter types against the registered required types
// 4. Registers the attribute if new, or returns existing AttrId
//
// Usage:
//   AttrPool pool;
//   register_builtin_attrs(pool);
//   // ... tokenize @[readonly]@ ...
//   auto result = parse_attr(pool, tokens, ctx.diag());
//   if (result) { token->add_attr(*result); }

#pragma once

#include "ast/attr_pool.hpp"
#include "ast/token_info.hpp"
#include "diag/context.hpp"
#include <optional>

namespace trust {

/// Result of parsing a single attribute from @[...] brackets.
struct ParsedAttr {
    AttrId m_id;                     ///< Resolved attribute ID
    std::vector<AttrParam> m_params; ///< Parsed parameter values
};

/// Parse an attribute from a token sequence representing @[...] content.
///
/// The token sequence should contain tokens between @[ and ]@ (exclusive).
/// Expected format:
///   name                    — attribute without parameters
///   name(param1, param2)    — attribute with parenthesized parameters
///
/// Parameters can be:
///   INTEGER   → int64_t param
///   STRWIDE   → string_view param
///   STRCHAR   → string_view param
///   NAME      → identifier (stored as string)
///
/// Returns nullopt on error (diagnostic issued via diag).
[[nodiscard]] std::optional<ParsedAttr> parse_attr(AttrPool& pool, const TokenSequence& tokens, DiagnosticEngine& diag);

} // namespace trust