// attr_parser.cpp - parse @[...] attribute name and parameters into AttrId

#include "ast/attr_parser.hpp"
#include "diag/base_diags.hpp"
#include "diag/diag.hpp"

namespace trust {

// ----------------------------------------------------------------------------
// parse_attr
// ----------------------------------------------------------------------------

std::optional<AttrId> parse_attr(Context& ctx, MapperRange range, std::string_view name, std::optional<std::vector<std::string_view>> params) {

    if (name.empty()) {
        ctx.diag().report(Severity::Error, range, "expected attribute name");
        return std::nullopt;
    }

    // Look up existing attribute
    auto existing_id = ctx.attrs().lookup(name);
    if (existing_id.has_value()) {
        // If parameters are supplied, validate them against the registered defaults.
        if (params.has_value()) {
            const Attr& attr = ctx.attrs().get(*existing_id);
            if (!attr.matches_params(*params)) {
                ctx.diag().report(Severity::Error, range, "attribute '{}' has mismatched parameters", name);
                return std::nullopt;
            }
        }
        return existing_id;
    }

    // Attribute not found - do NOT register a new one. Severity-controlled diagnostic:
    // -Wunknown-attributes (default Error; silence with =ignore). Если опция не зарегистрирована
    // (например parse-only путь без регистрации базовых диагностик) - жёсткий Error, как раньше.
    if (ctx.opts().isRegisteredByName(diagName(diag::DiagId::UnknownAttribute))) {
        ctx.report(range, diag::DiagId::UnknownAttribute, "unknown attribute '{}'", name);
    } else {
        ctx.diag().report(Severity::Error, range, "unknown attribute '{}'", name);
    }
    return std::nullopt;
}

} // namespace trust