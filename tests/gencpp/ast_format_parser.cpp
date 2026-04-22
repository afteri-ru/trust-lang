#include "ast_format_parser.hpp"
#include "diag/context.hpp"
#include "gencpp/ast.hpp"
#include "parser/mmproc.hpp"
#include <sstream>
#include <algorithm>
#include <expected>

namespace trust {

static int count_indent(const std::string &s) {
    int n = 0;
    while (n < (int)s.size() && (s[n] == ' ' || s[n] == '\t'))
        n++;
    return n;
}

static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static int offset_for_line_index(const std::string &text, int line_idx) {
    if (line_idx <= 0)
        return 0;
    int line = 0;
    for (int i = 0; i < (int)text.size(); i++) {
        if (text[i] == '\n') {
            line++;
            if (line == line_idx)
                return i + 1;
        }
    }
    return (int)text.size();
}

std::pair<std::string, std::string> parse_attr(const std::string &token) {
    if (token.size() >= 3 && token[0] == 'o' && token[1] == 'p' && token[2] == '=') {
        std::string op_val = token.substr(3);
        if (op_val == "=")
            return {"op", "=="};
        return {"op", op_val};
    }
    for (size_t i = 1; i < token.size(); ++i) {
        if (token[i] == '=') {
            bool prev_is_key_char = (token[i - 1] >= 'a' && token[i - 1] <= 'z') || (token[i - 1] >= 'A' && token[i - 1] <= 'Z') ||
                                    (token[i - 1] >= '0' && token[i - 1] <= '9') || (token[i - 1] == '_');
            bool next_is_eq = (i + 1 < token.size()) && (token[i + 1] == '=');
            if (prev_is_key_char && !next_is_eq) {
                return {token.substr(0, i), token.substr(i + 1)};
            }
        }
    }
    return {"", token};
}

// Internal parse with error propagation
static std::unique_ptr<ParsedNode> parse_nodes(std::vector<std::string> &lines, const std::string &text, SourceIdx src, size_t &pos, int parent_indent,
                                               std::string &error_msg) {
    if (pos >= lines.size())
        return nullptr;
    while (pos < lines.size()) {
        std::string trimmed = trim(lines[pos]);
        if (!trimmed.empty() && trimmed[0] != '#')
            break;
        pos++;
    }
    if (pos >= lines.size())
        return nullptr;

    int indent = count_indent(lines[pos]);
    if (indent <= parent_indent && pos > 0)
        return nullptr;

    int offset = offset_for_line_index(text, (int)pos);
    SourceLoc loc = SourceLoc::make(src, offset);

    std::istringstream iss(trim(lines[pos]));
    auto node = std::make_unique<ParsedNode>();
    node->loc = loc;
    std::string type_str;
    iss >> type_str;

    try {
        node->kind = AstTypeTraits::from_string(type_str);
    } catch (const std::invalid_argument &) {
        error_msg = "Unknown node type: '" + type_str + "'";
        return nullptr;
    }

    std::string token;
    while (iss >> token)
        node->attrs.push_back(parse_attr(token));
    pos++;

    while (pos < lines.size()) {
        while (pos < lines.size()) {
            std::string t = trim(lines[pos]);
            if (!t.empty() && t[0] != '#')
                break;
            pos++;
        }
        if (pos >= lines.size())
            break;
        if (count_indent(lines[pos]) <= indent)
            break;
        auto child = parse_nodes(lines, text, src, pos, indent, error_msg);
        if (child)
            node->children.push_back(std::move(child));
        else
            return nullptr;
    }
    return node;
}

std::expected<std::vector<std::unique_ptr<ParsedNode>>, std::string> parse_ast_format(const std::string &text, Context &ctx) {
    SourceIdx src = ctx.add_source("<ast-input>", text);

    std::istringstream is(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(is, line))
        lines.push_back(line);
    size_t pos = 0;
    std::vector<std::unique_ptr<ParsedNode>> roots;
    std::string error_msg;
    while (pos < lines.size()) {
        auto node = parse_nodes(lines, text, src, pos, -1, error_msg);
        if (node)
            roots.push_back(std::move(node));
        else {
            if (!error_msg.empty())
                return std::unexpected(error_msg);
            break;
        }
    }
    return roots;
}

// --- Builder (internal with std::expected) ---

static std::string get_attr(const ParsedNode *n, const std::string &key) {
    if (!n)
        return "";
    for (auto &[k, v] : n->attrs)
        if (k == key)
            return v;
    return "";
}

static bool has_attr(const ParsedNode *n, const std::string &key) {
    if (!n)
        return false;
    for (auto &[k, v] : n->attrs)
        if (k == key)
            return true;
    return false;
}

static auto parse_type_internal(const std::string &s, const std::string &context) -> std::expected<TypeInfo, std::string> {
    TypeInfo ti = TypeInfo::parse(s);
    if (ti.is_user()) {
        if (s == "usertype")
            return std::unexpected(context + ": invalid type 'usertype'");
    } else if (ti.to_string() == "int" && s != "int") {
        if (s != "string" && s != "void" && s != "bool")
            return std::unexpected(context + ": unknown type '" + s + "'");
    }
    return ti;
}

// Internal builder functions with std::expected
static std::expected<std::unique_ptr<Expr>, std::string> build_expr(const ParsedNode *n, Context &ctx);
static std::expected<std::unique_ptr<Stmt>, std::string> build_stmt(const ParsedNode *n, Context &ctx);
static std::expected<std::unique_ptr<Decl>, std::string> build_decl(const ParsedNode *n, Context &ctx);
static std::optional<BlockItem> build_block_item(const ParsedNode *n, Context &ctx);
// build_catch_block and build_matching_case are defined above

static BlockBody build_block_body(const ParsedNode *n, Context &ctx) {
    BlockBody body;
    for (auto &c : n->children) {
        auto item = build_block_item(c.get(), ctx);
        if (item)
            body.push_back(std::move(*item));
    }
    return body;
}

static std::optional<BlockItem> build_block_item(const ParsedNode *n, Context &ctx) {
    if (!n)
        return std::nullopt;
    TokenCategory cat = AstTypeTraits::node_category(n->kind);
    if (cat == TokenCategory::Decl) {
        auto d = build_decl(n, ctx);
        if (d.has_value())
            return std::move(*d);
    } else if (cat == TokenCategory::Stmt) {
        if (n->kind == ParserToken::Kind::ThenBlock || n->kind == ParserToken::Kind::ElseBlock || n->kind == ParserToken::Kind::MatchingElseBlock ||
            n->kind == ParserToken::Kind::MatchingCase || n->kind == ParserToken::Kind::WhileElseBlock) {
            return std::nullopt;
        }
        auto s = build_stmt(n, ctx);
        if (s.has_value())
            return std::move(*s);
    }
    return std::nullopt;
}

static std::unique_ptr<Expr> build_assignment_target(const std::string &target_str, Context &ctx) {
    auto bracket_pos = target_str.find('[');
    if (bracket_pos != std::string::npos) {
        auto dot_pos = target_str.find('.');
        std::string var_name;
        std::string field;
        if (dot_pos != std::string::npos) {
            var_name = target_str.substr(0, dot_pos);
            field = target_str.substr(dot_pos + 1);
        } else {
            var_name = target_str.substr(0, bracket_pos);
        }
        auto var_ref = std::make_unique<VarRef>(var_name);
        auto close_bracket = target_str.find(']', bracket_pos);
        std::string idx_str = target_str.substr(bracket_pos + 1, close_bracket - bracket_pos - 1);
        std::unique_ptr<Expr> idx;
        try {
            idx = std::make_unique<IntLiteral>(std::stoi(idx_str));
        } catch (...) {
            idx = std::make_unique<VarRef>(idx_str);
        }
        auto arr_access = std::make_unique<ArrayAccess>(std::move(var_ref), std::move(idx));
        if (!field.empty())
            return std::make_unique<MemberAccess>(std::move(arr_access), field);
        return arr_access;
    }
    auto dot_pos = target_str.find('.');
    if (dot_pos != std::string::npos) {
        auto obj = std::make_unique<VarRef>(target_str.substr(0, dot_pos));
        return std::make_unique<MemberAccess>(std::move(obj), target_str.substr(dot_pos + 1));
    }
    return std::make_unique<VarRef>(target_str);
}

static std::expected<std::unique_ptr<Expr>, std::string> build_expr(const ParsedNode *n, Context &ctx) {
    if (!n)
        return std::unexpected("null expression node");
    auto tk = n->kind;

    if (tk == ParserToken::Kind::IntLiteral) {
        if (!has_attr(n, "value"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'value'");
        std::string v = get_attr(n, "value");
        try {
            auto result = std::make_unique<IntLiteral>(std::stoi(v));
            result->loc = n->loc;
            return result;
        } catch (const std::invalid_argument &) {
            return std::unexpected("IntLiteral: invalid integer '" + v + "'");
        } catch (const std::out_of_range &) {
            return std::unexpected("IntLiteral: out of range '" + v + "'");
        }
    }
    if (tk == ParserToken::Kind::StringLiteral) {
        if (!has_attr(n, "value"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'value'");
        std::string v = get_attr(n, "value");
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
            v = v.substr(1, v.size() - 2);
            v = MMProcessor::unescape(v);
        }
        auto result = std::make_unique<StringLiteral>(v);
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::VarRef) {
        if (!has_attr(n, "name"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'name'");
        auto result = std::make_unique<VarRef>(get_attr(n, "name"));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::BinaryOp) {
        if (!has_attr(n, "op"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'op'");
        if (n->children.size() < 2)
            return std::unexpected(AstTypeTraits::to_string(tk) + " requires 2 children");
        auto op = MMProcessor::bin_op_from_string(get_attr(n, "op"));
        auto lhs = build_expr(n->children[0].get(), ctx);
        if (!lhs.has_value())
            return lhs;
        auto rhs = build_expr(n->children[1].get(), ctx);
        if (!rhs.has_value())
            return rhs;
        auto result = std::make_unique<BinaryOp>(op, std::move(*lhs), std::move(*rhs));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::CallExpr) {
        if (!has_attr(n, "name"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'name'");
        std::vector<std::unique_ptr<Expr>> args;
        for (auto &c : n->children) {
            auto arg = build_expr(c.get(), ctx);
            if (!arg.has_value())
                return std::unexpected(arg.error());
            args.push_back(std::move(*arg));
        }
        auto result = std::make_unique<CallExpr>(get_attr(n, "name"), std::move(args));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::EnumLiteral) {
        if (!has_attr(n, "enum"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'enum'");
        if (!has_attr(n, "member"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'member'");
        auto result = std::make_unique<EnumLiteral>(get_attr(n, "enum"), get_attr(n, "member"));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::MemberAccess) {
        if (!has_attr(n, "field"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'field'");
        if (n->children.empty())
            return std::unexpected(AstTypeTraits::to_string(tk) + " requires object child");
        auto obj = build_expr(n->children[0].get(), ctx);
        if (!obj.has_value())
            return obj;
        auto result = std::make_unique<MemberAccess>(std::move(*obj), get_attr(n, "field"));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::ArrayAccess) {
        if (n->children.size() < 2)
            return std::unexpected(AstTypeTraits::to_string(tk) + " requires 2 children");
        auto arr = build_expr(n->children[0].get(), ctx);
        if (!arr.has_value())
            return arr;
        auto idx = build_expr(n->children[1].get(), ctx);
        if (!idx.has_value())
            return idx;
        auto result = std::make_unique<ArrayAccess>(std::move(*arr), std::move(*idx));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::ArrayInit) {
        std::vector<std::unique_ptr<Expr>> elems;
        TypeKind elem_type = TypeKind::Int;
        if (has_attr(n, "type")) {
            auto ti = parse_type_internal(get_attr(n, "type"), AstTypeTraits::to_string(tk));
            if (!ti.has_value())
                return std::unexpected(ti.error());
            elem_type = ti->kind;
        }
        for (auto &c : n->children) {
            auto e = build_expr(c.get(), ctx);
            if (!e.has_value())
                return e;
            elems.push_back(std::move(*e));
        }
        auto result = std::make_unique<ArrayInit>(std::move(elems), elem_type);
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::CastExpr) {
        if (!has_attr(n, "type"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'type'");
        if (n->children.empty())
            return std::unexpected(AstTypeTraits::to_string(tk) + " requires an expression child");
        auto target_type = TypeInfo::parse(get_attr(n, "type"));
        auto expr = build_expr(n->children[0].get(), ctx);
        if (!expr.has_value())
            return expr;
        auto result = std::make_unique<CastExpr>(std::move(*expr), target_type);
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::RefMakeExpr) {
        if (n->children.empty())
            return std::unexpected(AstTypeTraits::to_string(tk) + " requires a child");
        auto child = build_expr(n->children[0].get(), ctx);
        if (!child.has_value())
            return child;
        auto result = std::make_unique<RefMakeExpr>(std::move(*child));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::RefTakeExpr) {
        if (n->children.empty())
            return std::unexpected(AstTypeTraits::to_string(tk) + " requires a child");
        auto child = build_expr(n->children[0].get(), ctx);
        if (!child.has_value())
            return child;
        auto result = std::make_unique<RefTakeExpr>(std::move(*child));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::EmbedExpr) {
        if (!has_attr(n, "value"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'value'");
        auto result = std::make_unique<EmbedExpr>(get_attr(n, "value"));
        result->loc = n->loc;
        return result;
    }
    return std::unexpected("Unknown Expr type: " + AstTypeTraits::to_string(tk));
}

// build_catch_block and build_matching_case - defined after build_expr/build_block_item
static std::unique_ptr<CatchBlock> build_catch_block(const ParsedNode *n, Context &ctx) {
    if (!n || n->kind != ParserToken::Kind::CatchBlock)
        return nullptr;
    if (!has_attr(n, "type") || !has_attr(n, "name"))
        return nullptr;
    auto type = parse_type_internal(get_attr(n, "type"), "CatchBlock");
    if (!type.has_value())
        return nullptr;
    std::string name = get_attr(n, "name");
    BlockBody body;
    for (auto &c : n->children) {
        auto item = build_block_item(c.get(), ctx);
        if (item.has_value())
            body.push_back(std::move(*item));
    }
    return std::make_unique<CatchBlock>(*type, std::move(name), std::move(body));
}

static std::unique_ptr<MatchingCase> build_matching_case(const ParsedNode *n, Context &ctx) {
    if (!n || n->kind != ParserToken::Kind::MatchingCase)
        return nullptr;
    std::unique_ptr<Expr> pattern = nullptr;
    if (!n->children.empty() && n->children[0]->is_expr()) {
        auto p = build_expr(n->children[0].get(), ctx);
        if (p.has_value())
            pattern = std::move(*p);
    }
    BlockBody body;
    size_t start = (pattern ? 1 : 0);
    for (size_t i = start; i < n->children.size(); ++i) {
        auto item = build_block_item(n->children[i].get(), ctx);
        if (item.has_value())
            body.push_back(std::move(*item));
    }
    return std::make_unique<MatchingCase>(std::move(pattern), std::move(body));
}

static std::expected<std::unique_ptr<Stmt>, std::string> build_stmt(const ParsedNode *n, Context &ctx) {
    if (!n)
        return std::unexpected("null statement node");
    auto tk = n->kind;

    if (tk == ParserToken::Kind::AssignmentStmt) {
        if (!has_attr(n, "target"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'target'");
        std::string target_str = get_attr(n, "target");
        std::unique_ptr<Expr> target = build_assignment_target(target_str, ctx);
        std::unique_ptr<Expr> value = nullptr;
        if (!n->children.empty()) {
            auto v = build_expr(n->children[0].get(), ctx);
            if (!v.has_value())
                return std::unexpected(v.error());
            value = std::move(*v);
        }
        auto result = std::make_unique<AssignmentStmt>(std::move(target), std::move(value));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::ReturnStmt) {
        std::unique_ptr<Expr> val = nullptr;
        if (!n->children.empty()) {
            auto v = build_expr(n->children[0].get(), ctx);
            if (!v.has_value())
                return std::unexpected(v.error());
            val = std::move(*v);
        }
        auto result = std::make_unique<ReturnStmt>(std::move(val));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::ExprStmt) {
        std::unique_ptr<Expr> expr = nullptr;
        if (!n->children.empty()) {
            auto e = build_expr(n->children[0].get(), ctx);
            if (!e.has_value())
                return std::unexpected(e.error());
            expr = std::move(*e);
        }
        auto result = std::make_unique<ExprStmt>(std::move(expr));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::BlockStmt) {
        auto result = std::make_unique<BlockStmt>(build_block_body(n, ctx));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::IfStmt) {
        std::unique_ptr<Expr> cond;
        size_t i = 0;
        if (i < n->children.size() && n->children[i]->is_expr()) {
            auto c = build_expr(n->children[i].get(), ctx);
            if (!c.has_value())
                return std::unexpected(c.error());
            cond = std::move(*c);
            i++;
        }
        BlockBody then_body;
        while (i < n->children.size() && n->children[i]->kind != ParserToken::Kind::ElseBlock) {
            if (n->children[i]->kind == ParserToken::Kind::ThenBlock) {
                for (auto &c : n->children[i]->children) {
                    auto item = build_block_item(c.get(), ctx);
                    if (item.has_value())
                        then_body.push_back(std::move(*item));
                }
            } else {
                auto item = build_block_item(n->children[i].get(), ctx);
                if (item.has_value())
                    then_body.push_back(std::move(*item));
            }
            i++;
        }
        std::unique_ptr<IfStmt> else_if;
        std::unique_ptr<BlockStmt> else_block;
        if (i < n->children.size() && n->children[i]->kind == ParserToken::Kind::ElseBlock) {
            auto &else_blk = n->children[i];
            if (else_blk->children.size() == 1 && else_blk->children[0]->kind == ParserToken::Kind::IfStmt) {
                auto elif_node = build_stmt(else_blk->children[0].get(), ctx);
                if (!elif_node)
                    return elif_node;
                else_if = std::unique_ptr<IfStmt>(static_cast<IfStmt *>(elif_node->release()));
            } else {
                BlockBody else_nodes;
                for (auto &c : else_blk->children) {
                    auto item = build_block_item(c.get(), ctx);
                    if (item.has_value())
                        else_nodes.push_back(std::move(*item));
                }
                if (!else_nodes.empty())
                    else_block = std::make_unique<BlockStmt>(std::move(else_nodes));
            }
        }
        auto result = std::make_unique<IfStmt>(std::move(cond), std::move(then_body), std::move(else_if), std::move(else_block));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::ThenBlock || tk == ParserToken::Kind::ElseBlock)
        return std::unexpected(AstTypeTraits::to_string(tk) + " is not a standalone statement");
    if (tk == ParserToken::Kind::WhileStmt) {
        std::unique_ptr<Expr> cond;
        size_t i = 0;
        if (i < n->children.size() && n->children[i]->is_expr()) {
            auto c = build_expr(n->children[i].get(), ctx);
            if (!c.has_value())
                return std::unexpected(c.error());
            cond = std::move(*c);
            i++;
        }
        BlockBody body;
        BlockBody else_body;
        for (; i < n->children.size(); ++i) {
            if (n->children[i]->kind == ParserToken::Kind::WhileElseBlock) {
                for (auto &c : n->children[i]->children) {
                    auto item = build_block_item(c.get(), ctx);
                    if (item.has_value())
                        else_body.push_back(std::move(*item));
                }
            } else {
                auto item = build_block_item(n->children[i].get(), ctx);
                if (item.has_value())
                    body.push_back(std::move(*item));
            }
        }
        auto result = std::make_unique<WhileStmt>(std::move(cond), std::move(body), std::move(else_body));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::DoWhileStmt) {
        BlockBody body;
        std::unique_ptr<Expr> cond;
        size_t i = 0;
        if (!n->children.empty() && n->children.back()->is_expr()) {
            auto c = build_expr(n->children.back().get(), ctx);
            if (!c.has_value())
                return std::unexpected(c.error());
            cond = std::move(*c);
            for (i = 0; i + 1 < n->children.size(); ++i) {
                auto item = build_block_item(n->children[i].get(), ctx);
                if (item.has_value())
                    body.push_back(std::move(*item));
            }
        }
        auto result = std::make_unique<DoWhileStmt>(std::move(body), std::move(cond));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::TryCatchStmt) {
        size_t i = 0;
        BlockBody try_body;
        for (; i < n->children.size() && n->children[i]->kind != ParserToken::Kind::CatchBlock; ++i) {
            auto item = build_block_item(n->children[i].get(), ctx);
            if (item.has_value())
                try_body.push_back(std::move(*item));
        }
        std::unique_ptr<CatchBlock> catch_blk;
        if (i < n->children.size() && n->children[i]->kind == ParserToken::Kind::CatchBlock) {
            catch_blk = build_catch_block(n->children[i].get(), ctx);
            if (!catch_blk)
                return std::unexpected("failed to build catch block");
        }
        auto result = std::make_unique<TryCatchStmt>(std::move(try_body), std::move(catch_blk));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::ThrowStmt) {
        std::unique_ptr<Expr> val = nullptr;
        if (!n->children.empty()) {
            auto v = build_expr(n->children[0].get(), ctx);
            if (!v.has_value())
                return std::unexpected(v.error());
            val = std::move(*v);
        }
        auto result = std::make_unique<ThrowStmt>(std::move(val));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::MatchingStmt) {
        std::unique_ptr<Expr> expr;
        size_t i = 0;
        if (i < n->children.size() && n->children[i]->is_expr()) {
            auto e = build_expr(n->children[i].get(), ctx);
            if (!e.has_value())
                return std::unexpected(e.error());
            expr = std::move(*e);
            i++;
        }
        std::vector<std::unique_ptr<MatchingCase>> cases;
        BlockBody else_body;
        for (; i < n->children.size(); ++i) {
            if (n->children[i]->kind == ParserToken::Kind::MatchingCase) {
                cases.push_back(build_matching_case(n->children[i].get(), ctx));
            } else if (n->children[i]->kind == ParserToken::Kind::MatchingElseBlock) {
                for (auto &c : n->children[i]->children) {
                    auto item = build_block_item(c.get(), ctx);
                    if (item.has_value())
                        else_body.push_back(std::move(*item));
                }
            }
        }
        auto result = std::make_unique<MatchingStmt>(std::move(expr), std::move(cases), std::move(else_body));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::MatchingCase || tk == ParserToken::Kind::MatchingElseBlock || tk == ParserToken::Kind::WhileElseBlock)
        return std::unexpected(AstTypeTraits::to_string(tk) + " is not a standalone statement");
    if (tk == ParserToken::Kind::BreakStmt) {
        auto result = std::make_unique<BreakStmt>();
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::ContinueStmt) {
        auto result = std::make_unique<ContinueStmt>();
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::VarDecl || tk == ParserToken::Kind::FuncDecl || tk == ParserToken::Kind::ParamDecl || tk == ParserToken::Kind::EnumDecl ||
        tk == ParserToken::Kind::EnumMember || tk == ParserToken::Kind::StructDecl || tk == ParserToken::Kind::StructField) {
        return std::unexpected("build_statement(): " + AstTypeTraits::to_string(tk) + " is a declaration");
    }
    return std::unexpected("Unknown Stmt type: " + AstTypeTraits::to_string(tk));
}

static std::expected<std::unique_ptr<Decl>, std::string> build_decl(const ParsedNode *n, Context &ctx) {
    if (!n)
        return std::unexpected("null declaration node");
    auto tk = n->kind;

    if (tk == ParserToken::Kind::VarDecl) {
        if (!has_attr(n, "name"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'name'");
        std::unique_ptr<Expr> init = nullptr;
        if (!n->children.empty()) {
            auto i = build_expr(n->children[0].get(), ctx);
            if (!i.has_value())
                return std::unexpected(i.error());
            init = std::move(*i);
        }
        if (has_attr(n, "type")) {
            auto type = TypeInfo::parse(get_attr(n, "type"));
            auto result = std::make_unique<VarDecl>(get_attr(n, "name"), type, std::move(init));
            result->loc = n->loc;
            return result;
        } else {
            auto result = std::make_unique<VarDecl>(get_attr(n, "name"), std::move(init));
            result->loc = n->loc;
            return result;
        }
    }
    if (tk == ParserToken::Kind::FuncDecl) {
        if (!has_attr(n, "name"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'name'");
        if (!has_attr(n, "ret"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'ret'");
        std::string name = get_attr(n, "name");
        auto ret_ti = parse_type_internal(get_attr(n, "ret"), "FuncDecl");
        if (!ret_ti.has_value())
            return std::unexpected(ret_ti.error());
        std::vector<std::unique_ptr<ParamDecl>> params;
        std::unique_ptr<BlockStmt> body_ptr = nullptr;
        for (auto &c : n->children) {
            if (c->kind == ParserToken::Kind::ParamDecl) {
                if (!has_attr(c.get(), "name") || !has_attr(c.get(), "type"))
                    return std::unexpected(AstTypeTraits::to_string(c->kind) + ": missing 'name'/'type'");
                auto param = std::make_unique<ParamDecl>(get_attr(c.get(), "name"), TypeInfo::parse(get_attr(c.get(), "type")));
                param->loc = c->loc;
                params.push_back(std::move(param));
            } else if (c->kind == ParserToken::Kind::BlockStmt) {
                body_ptr = std::make_unique<BlockStmt>(build_block_body(c.get(), ctx));
                body_ptr->loc = c->loc;
            }
        }
        auto result = std::make_unique<FuncDecl>(name, std::move(*ret_ti), std::move(params), std::move(body_ptr));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::EnumDecl) {
        if (!has_attr(n, "name"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'name'");
        std::vector<std::unique_ptr<EnumMember>> members;
        for (auto &c : n->children) {
            if (c->kind == ParserToken::Kind::EnumMember) {
                if (!has_attr(c.get(), "name"))
                    return std::unexpected(AstTypeTraits::to_string(c->kind) + ": missing 'name'");
                std::string mname = get_attr(c.get(), "name");
                if (has_attr(c.get(), "value")) {
                    int v = std::stoi(get_attr(c.get(), "value"));
                    auto member = std::make_unique<EnumMember>(std::move(mname), std::make_unique<int>(v));
                    member->loc = c->loc;
                    members.push_back(std::move(member));
                } else {
                    auto member = std::make_unique<EnumMember>(std::move(mname), nullptr);
                    member->loc = c->loc;
                    members.push_back(std::move(member));
                }
            }
        }
        auto result = std::make_unique<EnumDecl>(get_attr(n, "name"), std::move(members));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::EnumMember) {
        if (!has_attr(n, "name"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'name'");
        std::string mname = get_attr(n, "name");
        if (has_attr(n, "value")) {
            int v = std::stoi(get_attr(n, "value"));
            auto result = std::make_unique<EnumMember>(std::move(mname), std::make_unique<int>(v));
            result->loc = n->loc;
            return result;
        } else {
            auto result = std::make_unique<EnumMember>(std::move(mname), nullptr);
            result->loc = n->loc;
            return result;
        }
    }
    if (tk == ParserToken::Kind::StructDecl) {
        if (!has_attr(n, "name"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'name'");
        std::vector<std::unique_ptr<StructField>> fields;
        std::vector<std::unique_ptr<Decl>> method_items;
        for (auto &c : n->children) {
            if (c->kind == ParserToken::Kind::StructField) {
                if (!has_attr(c.get(), "name"))
                    return std::unexpected(AstTypeTraits::to_string(c->kind) + ": missing 'name'");
                if (!has_attr(c.get(), "type"))
                    return std::unexpected(AstTypeTraits::to_string(c->kind) + ": missing 'type'");
                TypeInfo tf = TypeInfo::parse(get_attr(c.get(), "type"));
                std::unique_ptr<Expr> init = nullptr;
                for (auto &cc : c->children) {
                    if (cc->is_expr()) {
                        auto e = build_expr(cc.get(), ctx);
                        if (!e.has_value())
                            return std::unexpected(e.error());
                        init = std::move(*e);
                    } else if (cc->kind == ParserToken::Kind::VarDecl) {
                        auto vd = build_decl(cc.get(), ctx);
                        if (!vd.has_value())
                            return std::unexpected(vd.error());
                        if (auto *v = vd->get()->as<VarDecl>())
                            init = std::move(v->init);
                    }
                }
                auto field = std::make_unique<StructField>(get_attr(c.get(), "name"), tf, std::move(init));
                field->loc = c->loc;
                fields.push_back(std::move(field));
            } else if (c->kind == ParserToken::Kind::FuncDecl) {
                auto method = build_decl(c.get(), ctx);
                if (!method.has_value())
                    return std::unexpected(method.error());
                method_items.push_back(std::move(*method));
            }
        }
        auto result = std::make_unique<StructDecl>(get_attr(n, "name"), std::move(fields), std::move(method_items));
        result->loc = n->loc;
        return result;
    }
    if (tk == ParserToken::Kind::StructField) {
        if (!has_attr(n, "name"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'name'");
        if (!has_attr(n, "type"))
            return std::unexpected(AstTypeTraits::to_string(tk) + ": missing required attribute 'type'");
        TypeInfo tf = TypeInfo::parse(get_attr(n, "type"));
        std::unique_ptr<Expr> init = nullptr;
        for (auto &c : n->children) {
            if (c->is_expr()) {
                auto e = build_expr(c.get(), ctx);
                if (!e.has_value())
                    return std::unexpected(e.error());
                init = std::move(*e);
            }
        }
        auto result = std::make_unique<StructField>(get_attr(n, "name"), tf, std::move(init));
        result->loc = n->loc;
        return result;
    }
    return std::unexpected("Unknown Decl type: " + AstTypeTraits::to_string(tk));
}

// --- Public API wrappers ---

std::unique_ptr<Expr> build_expression(const ParsedNode *n, Context &ctx) {
    auto r = build_expr(n, ctx);
    if (r.has_value())
        return std::move(*r);
    return nullptr;
}

std::unique_ptr<Stmt> build_statement(const ParsedNode *n, Context &ctx) {
    auto r = build_stmt(n, ctx);
    if (r.has_value())
        return std::move(*r);
    return nullptr;
}

std::unique_ptr<Decl> build_declaration(const ParsedNode *n, Context &ctx) {
    auto r = build_decl(n, ctx);
    if (r.has_value())
        return std::move(*r);
    return nullptr;
}

std::unique_ptr<Program> build_ast_from_roots(const std::vector<ParsedNode *> &roots, Context &ctx) {
    std::vector<std::unique_ptr<Decl>> items;
    std::vector<BlockItem> stmt_body;
    for (auto *node : roots) {
        auto cat = AstTypeTraits::node_category(node->kind);
        if (cat == TokenCategory::Decl || cat == TokenCategory::Root) {
            auto decl = build_declaration(node, ctx);
            if (decl)
                items.push_back(std::move(decl));
        } else {
            auto s = build_statement(node, ctx);
            if (s)
                stmt_body.push_back(std::move(s));
        }
    }
    if (!stmt_body.empty()) {
        std::vector<std::unique_ptr<ParamDecl>> no_params;
        items.push_back(std::make_unique<FuncDecl>("main", TypeKind::Int, std::move(no_params), std::make_unique<BlockStmt>(std::move(stmt_body))));
    }
    return std::make_unique<Program>(std::move(items));
}

} // namespace trust