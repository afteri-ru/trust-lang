// attr_parser.cpp — parse @[...] attribute tokens into AttrId

#include "ast/attr_parser.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include <cctype>

namespace trust {

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

/// Convert a token kind to an AttrParam (checks for integer/string/identifier tokens)
static std::optional<AttrParam> token_to_param(const TokenPtr& tok) {
    using PK = ParserToken::Kind;

    // Integer literal
    if (tok->kind == PK::INTEGER || tok->kind == PK::NUMBER) {
        // Simple conversion: try to parse as int64_t
        char* end = nullptr;
        int64_t val = std::strtoll(tok->text.c_str(), &end, 10);
        if (end != tok->text.c_str() && *end == '\0') {
            return AttrParam(val);
        }
        return std::nullopt;
    }

    // String literals
    if (tok->kind == PK::STRWIDE || tok->kind == PK::STRCHAR) {
        return AttrParam(std::string_view(tok->text));
    }

    // Identifier (NAME) — treat as string parameter
    if (tok->kind == PK::NAME) {
        return AttrParam(std::string_view(tok->text));
    }

    return std::nullopt;
}

/// Check if the given params' types match the required types
static bool validate_param_types(const std::vector<AttrParam>& params, const std::vector<AttrParamType>& required, DiagnosticEngine& diag, MapperRange range) {
    if (params.size() != required.size()) {
        diag.report(range, Severity::Error, "attribute requires {} parameters, but {} provided", required.size(), params.size());
        return false;
    }

    for (std::size_t i = 0; i < params.size(); ++i) {
        AttrParamType got = params[i].type();
        AttrParamType expected = required[i];
        if (got != expected) {
            diag.report(range, Severity::Error, "attribute parameter {}: expected type {}, got type {}", i + 1, static_cast<int>(expected),
                        static_cast<int>(got));
            return false;
        }
    }
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// parse_attr
// ────────────────────────────────────────────────────────────────────────────

std::optional<ParsedAttr> parse_attr(AttrPool& pool, const TokenSequence& tokens, DiagnosticEngine& diag) {
    using PK = ParserToken::Kind;

    if (tokens.empty()) {
        return std::nullopt;
    }

    // The first meaningful token must be the attribute name
    // (we skip ATTR_BEGIN/ATTR_END — they are not in the sequence per API contract)
    std::size_t pos = 0;

    // Skip non-name tokens (e.g. whitespace-related or empty tokens)
    while (pos < tokens.size() && tokens[pos]->kind != PK::NAME) {
        ++pos;
    }

    if (pos >= tokens.size()) {
        diag.report(MapperRange{}, Severity::Error, "expected attribute name");
        return std::nullopt;
    }

    // Extract attribute name
    std::string attr_name = tokens[pos]->text;
    MapperRange attr_range = tokens[pos]->range;
    ++pos;

    // Check for parenthesized parameters
    std::vector<AttrParam> params;
    bool has_parens = false;

    if (pos < tokens.size() && tokens[pos]->kind == PK::LPAREN) {
        has_parens = true;
        ++pos; // skip '('

        // Collect parameters until ')'
        while (pos < tokens.size() && tokens[pos]->kind != PK::RPAREN) {
            // Skip commas
            if (tokens[pos]->kind == PK::COMMA) {
                ++pos;
                continue;
            }

            auto param = token_to_param(tokens[pos]);
            if (!param.has_value()) {
                diag.report(tokens[pos]->range, Severity::Error, "invalid attribute parameter '{}'", tokens[pos]->text);
                return std::nullopt;
            }

            params.push_back(std::move(param.value()));
            ++pos;
        }

        if (pos >= tokens.size() || tokens[pos]->kind != PK::RPAREN) {
            diag.report(attr_range, Severity::Error, "unterminated '(' in attribute parameters");
            return std::nullopt;
        }
        ++pos; // skip ')'
    }

    // Now we have the attribute name and params (if any).
    // Try to look up existing attribute registration.
    auto existing_id = pool.lookup(attr_name);

    if (existing_id.has_value()) {
        const Attr& existing = pool.get(existing_id.value());

        // Validate parameter types against required types
        if (!validate_param_types(params, existing.m_required_param_types, diag, attr_range)) {
            return std::nullopt;
        }

        return ParsedAttr{existing_id.value(), std::move(params)};
    }

    // Attribute not found — register as user-defined with the provided params
    AttrId new_id;
    if (has_parens) {
        new_id = pool.register_attr(attr_name, std::move(params));
    } else {
        new_id = pool.register_attr(attr_name, std::vector<AttrParamType>{});
    }

    return ParsedAttr{new_id, std::vector<AttrParam>{}};
}

} // namespace trust