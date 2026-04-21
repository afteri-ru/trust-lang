#include "gencpp/ast.hpp"
#include "types/type_info.hpp"
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <format>

namespace trust {

static TokenCategory token_category_from_type(ParserToken::Kind k) noexcept {
    auto flags = ParserToken::flags(k);
    if ((flags & TokenFlag::Expr) != TokenFlag{}) return TokenCategory::Expr;
    if ((flags & TokenFlag::Stmt) != TokenFlag{}) return TokenCategory::Stmt;
    if ((flags & TokenFlag::Decl) != TokenFlag{}) return TokenCategory::Decl;
    if ((flags & TokenFlag::Root) != TokenFlag{}) return TokenCategory::Root;
    return TokenCategory::None;
}

// AstTypeTraits — keep for compatibility, now wrapping ParserToken
std::string AstTypeTraits::to_string(ParserToken::Kind t) {
    std::string_view sv = ParserToken::name(t);
    return std::string(sv);
}

ParserToken::Kind AstTypeTraits::from_string(const std::string &s) {
    if (auto* pk = ParserToken::from_name(s)) return *pk;
    throw std::invalid_argument("Unknown node type: '" + s + "'");
}

// ==========================
// BinOpUtils implementations
// ==========================

std::string BinOpUtils::to_string(BinOp op) {
    switch (op) {
    case BinOp::Add: return "+";
    case BinOp::Sub: return "-";
    case BinOp::Mul: return "*";
    case BinOp::Div: return "/";
    case BinOp::Eq: return "==";
    case BinOp::Ne: return "!=";
    case BinOp::Lt: return "<";
    case BinOp::Le: return "<=";
    case BinOp::Gt: return ">";
    case BinOp::Ge: return ">=";
    case BinOp::And: return "and";
    case BinOp::Or: return "or";
    default:
        throw std::invalid_argument(std::format("Unknown BinOp: '{}'", static_cast<int>(op)));
    }
}

BinOp BinOpUtils::from_string(const std::string &s) {
    if (s == "+") return BinOp::Add;
    if (s == "-") return BinOp::Sub;
    if (s == "*") return BinOp::Mul;
    if (s == "/") return BinOp::Div;
    if (s == "==") return BinOp::Eq;
    if (s == "!=") return BinOp::Ne;
    if (s == "<") return BinOp::Lt;
    if (s == "<=") return BinOp::Le;
    if (s == ">") return BinOp::Gt;
    if (s == ">=") return BinOp::Ge;
    if (s == "and") return BinOp::And;
    if (s == "or") return BinOp::Or;
    throw std::invalid_argument("Unknown BinOp: '" + s + "'");
}

// ==========================
// StringUtils implementations
// ==========================

std::string StringUtils::escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\0': out += "\\0";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

std::string StringUtils::unescape(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
            case '"':  out += '"'; i++; break;
            case '\\': out += '\\'; i++; break;
            case 'n':  out += '\n'; i++; break;
            case 'r':  out += '\r'; i++; break;
            case 't':  out += '\t'; i++; break;
            case '0':  out += '\0'; i++; break;
            default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

} // namespace trust