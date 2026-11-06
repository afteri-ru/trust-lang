// Parse @[...] attribute name and parameters into AttrId.
// Works directly with string_view values - no dependency on TokenSequence, Lexeme, or ParserToken.

#pragma once

#include "attrs/attr_pool.hpp"
#include "ast/convert_services.hpp"
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace trust {

/// Parse an attribute from a name and optional parameter values.
/// Expected format:
///   name                    - attribute without parameters
///   name(param1, param2)    - attribute with parenthesized parameters
/// Parameters are string_view values (raw text from source).
/// The attribute must already be registered in AttrPool. If it is not found,
/// a diagnostic error "unknown attribute" is issued and nullopt is returned.
/// Registration is performed separately (register_attr / register_builtin_attr).
/// Returns nullopt on error (diagnostic issued via ctx.diag()).
[[nodiscard]] std::optional<AttrId> parse_attr(ConvertServices& ctx, MapperRange range, std::string_view name,
                                               std::optional<std::vector<std::string_view>> params = std::nullopt);

template <class Ctx>
    requires(!std::is_same_v<std::remove_cvref_t<Ctx>, ConvertServices>)
std::optional<AttrId> parse_attr(Ctx& ctx, MapperRange range, std::string_view name,
                                 std::optional<std::vector<std::string_view>> params = std::nullopt) {
    ConvertServices services = makeConvertServices(ctx);
    return parse_attr(services, range, name, std::move(params));
}

} // namespace trust