#pragma once

#include "gencpp/ast.hpp"
#include <expected>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace trust {

class Context;

// Forward declarations
class Program;
class Expr;
class Stmt;
class Decl;

// Parsed node with token-based type and location
struct ParsedNode {
    ParserToken::Kind kind;
    SourceLoc loc;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::unique_ptr<ParsedNode>> children;

    TokenCategory get_category() const {
        return AstTypeTraits::node_category(kind);
    }

    bool is_expr() const { return AstTypeTraits::is_expr_node(kind); }
    bool is_stmt() const { return AstTypeTraits::is_stmt_node(kind); }
    bool is_decl() const { return AstTypeTraits::is_decl_node(kind); }
    bool is_root() const { return AstTypeTraits::is_root_node(kind); }
};

// Parsing
std::pair<std::string, std::string> parse_attr(const std::string &token);

// Parse AST from text format. Returns error message on parse failure.
// SourceIdx is managed internally via ctx.add_source().
std::expected<std::vector<std::unique_ptr<ParsedNode>>, std::string>
parse_ast_format(const std::string& text, Context& ctx);

// Building
std::unique_ptr<Program> build_ast_from_roots(const std::vector<ParsedNode *> &roots, Context& ctx);
std::unique_ptr<Expr> build_expression(const ParsedNode *n, Context& ctx);
std::unique_ptr<Stmt> build_statement(const ParsedNode *n, Context& ctx);
std::unique_ptr<Decl> build_declaration(const ParsedNode *n, Context& ctx);

} // namespace trust