// attr_parser.cpp — parse @[...] attribute name and parameters into AttrId

#include "ast/attr_parser.hpp"
#include "diag/diag.hpp"

namespace trust {

// ────────────────────────────────────────────────────────────────────────────
// parse_attr
// ────────────────────────────────────────────────────────────────────────────

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

    // Attribute not found — diagnostic error, do NOT register a new one.
    ctx.diag().report(Severity::Error, range, "unknown attribute '{}'", name);
    return std::nullopt;
}

} // namespace trust