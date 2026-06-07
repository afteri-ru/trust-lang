// attr_parser.hpp — parse @[...] attribute name and parameters into AttrId
//
// Works directly with string_view values — no dependency on TokenSequence,
// Lexeme, or ParserToken.
//
// Usage:
//   Context ctx;
//   auto result = parse_attr(ctx, range, "readonly");
//   if (result) { token->add_attr(*result); }

#pragma once

#include "ast/attr_pool.hpp"
#include "diag/context.hpp"
#include <optional>
#include <string_view>
#include <vector>

namespace trust {

/// Parse an attribute from a name and optional parameter values.
///
/// Expected format:
///   name                    — attribute without parameters
///   name(param1, param2)    — attribute with parenthesized parameters
///
/// Parameters are string_view values (raw text from source).
///
/// The attribute must already be registered in AttrPool. If it is not found,
/// a diagnostic error "unknown attribute" is issued and nullopt is returned.
/// Registration is performed separately (register_attr / register_builtin_attr).
///
/// Returns nullopt on error (diagnostic issued via ctx.diag()).
[[nodiscard]] std::optional<AttrId> parse_attr(Context& ctx, MapperRange range, std::string_view name,
                                               std::optional<std::vector<std::string_view>> params = std::nullopt);

} // namespace trust