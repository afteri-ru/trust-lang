#include "semantic/analyzer.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "types/type_id.hpp"
#include "types/registry.hpp"

namespace trust {

SemanticAnalyzer::SemanticAnalyzer(Context& ctx)
: m_ctx(ctx)
, m_symbols(std::make_unique<SymbolTable>(ctx.diag())) {
}

bool SemanticAnalyzer::analyze(const std::vector<AstNodePtr>& ast_nodes) {
    for (const auto& node : ast_nodes) {
        if (node)
            analyzeNode(*node);
    }
    return m_ctx.diag().errorCount() == 0;
}

void SemanticAnalyzer::analyzeNode(const AstNodeBase& node) {
    if (node.kind() == ParserToken::Kind::END)
        return;

    // Sequence → walk body
    if (node.kind() == ParserToken::Kind::sequence) {
        auto* scope = static_cast<const Sequence*>(&node);
        if (scope) {
            for (const auto& child : scope->m_body) {
                if (child)
                    analyzeNode(*child);
            }
        }
        return;
    }

    // VarDecl → variable declaration
    if (node.kind() == ParserToken::Kind::VarDecl) {
        auto* var = static_cast<const VarDecl*>(&node);
        if (var) {
            analyzeVarDecl(*var);
        }
        return;
    }

    // BinaryOp → check specific operator (::= for type decl)
    if (is_binary_kind(node.kind())) {
        auto* binary = static_cast<const Binary*>(&node);
        if (binary && binary->kind() == ParserToken::Kind::TypeDecl) {
            analyzeTypeDecl(*binary);
        }
        // Other binary nodes (assignments, math ops) are skipped in Phase 1
        return;
    }

    // FuncDecl → function declaration
    if (node.kind() == ParserToken::Kind::FuncDecl) {
        auto* func = static_cast<const FuncDecl*>(&node);
        if (func) {
            analyzeFuncDecl(*func);
        }
        return;
    }

    // Standalone Ident → check that it was declared
    if (node.kind() == ParserToken::Kind::Ident) {
        lookupOrError(node);
        return;
    }

    // Standalone literals are always valid semantically
    if (node.kind() == ParserToken::Kind::IntLiteral || node.kind() == ParserToken::Kind::StringLiteral) {
        return;
    }

    // Unknown node — skip (parser handles syntax errors)
}

void SemanticAnalyzer::analyzeVarDecl(const VarDecl& var_node) {
    std::string var_name{var_node.text()};
    MapperRange var_range = var_node.range();

    // Resolve optional type annotation
    TypeId var_type = INVALID_TYPE_ID;
    if (var_node.m_type) {
        if (var_node.m_type->kind() == ParserToken::Kind::TypeName) {
            auto type_id = m_ctx.types().findType(var_node.m_type->text());
            if (type_id.has_value()) {
                var_type = *type_id;
            } else {
                m_ctx.diag().report(Severity::Error, var_node.m_type->range(), "unknown type '{}'", var_node.m_type->text());
            }
        }
    }

    // Get initializer if present
    AstNodePtr init_node = var_node.m_initializer;

    if (!init_node) {
        m_ctx.diag().report(Severity::Error, var_range, "variable '{}' must have an initializer", var_name);
        return;
    }

    // Check for duplicate
    Symbol sym;
    sym.name = var_name;
    sym.type = var_type;
    sym.sourceRange = var_range;
    sym.data = VariableSymbolData{init_node, var_node.m_is_mutable};

    if (!m_symbols->addSymbol(std::move(sym)))
        return;

    // Walk the initializer expression looking for idents
    if (init_node->kind() == ParserToken::Kind::Ident) {
        lookupOrError(*init_node);
    } else if (is_binary_kind(init_node->kind())) {
        walkExprForIdents(*static_cast<const Binary*>(init_node.get()));
    }
}

void SemanticAnalyzer::analyzeTypeDecl(const Binary& binary_node) {
    auto* left = binary_node.m_left.get();
    if (!left || left->kind() != ParserToken::Kind::Ident) {
        m_ctx.diag().report(Severity::Error, binary_node.range(), "type declaration must have a name on the left");
        return;
    }

    std::string type_name = std::string(left->text());

    auto* right = binary_node.m_right.get();
    if (!right) {
        m_ctx.diag().report(Severity::Error, binary_node.range(), "type '{}' must have a definition", type_name);
        return;
    }

    // Right can be: TypeName (e.g. Int), Ident (existing type), or literal
    if (right->kind() == ParserToken::Kind::TypeName) {
        // y ::= Int; — alias to existing type
        TypeId base_id = m_ctx.types().findType(right->text()).value_or(INVALID_TYPE_ID);
        if (base_id == INVALID_TYPE_ID) {
            m_ctx.diag().report(Severity::Error, right->range(), "type '{}' not found", right->text());
            return;
        }

        // Register new type as alias
        m_ctx.types().registerType(type_name, base_id, {}, right->range());
    } else if (right->kind() == ParserToken::Kind::Ident) {
        // y ::= existing_var; — alias to variable's type? For now, just lookup
        const auto* sym = m_symbols->lookup(right->text());
        if (!sym) {
            lookupOrError(*right);
        }
    }
}

void SemanticAnalyzer::analyzeFuncDecl(const FuncDecl& func_node) {
    // Function name (IdentName) is available via inherited text()
    std::string func_name{func_node.text()};
    MapperRange func_range = func_node.range();

    // Just register the function name to avoid "undefined name" errors
    // Full type analysis will be done in a later phase
    TypeId funcTypeId = INVALID_TYPE_ID;

    // Check for duplicate name
    const auto* existing = m_symbols->lookup(func_name);
    if (existing) {
        if (!existing->sourceRange.isInvalid()) {
            m_ctx.diag().report(Severity::Error, existing->sourceRange, "previous declaration of '{}'", func_name);
        }
        m_ctx.diag().report(Severity::Error, func_range, "duplicate declaration of '{}'", func_name);
        return;
    }

    // Register symbol
    Symbol sym;
    sym.name = func_name;
    sym.type = funcTypeId;
    sym.sourceRange = func_range;
    sym.data = FunctionSymbolData{nullptr};
    m_symbols->addSymbol(std::move(sym));
}

const Symbol* SemanticAnalyzer::lookupOrError(const AstNodeBase& node) {
    auto name = node.text();
    auto* sym = m_symbols->lookup(name);
    if (!sym) {
        m_ctx.diag().report(Severity::Error, node.range(), "undefined name '{}'", name);
        return nullptr;
    }
    return sym;
}

void SemanticAnalyzer::walkExprForIdents(const Binary& node) {
    // Walk left
    if (node.m_left) {
        auto* left = node.m_left.get();
        if (left->kind() == ParserToken::Kind::Ident) {
            lookupOrError(*left);
        } else if (is_binary_kind(left->kind())) {
            walkExprForIdents(*static_cast<const Binary*>(left));
        }
    }
    // Walk right
    if (node.m_right) {
        auto* right = node.m_right.get();
        if (right->kind() == ParserToken::Kind::Ident) {
            lookupOrError(*right);
        } else if (is_binary_kind(right->kind())) {
            walkExprForIdents(*static_cast<const Binary*>(right));
        }
    }
}

} // namespace trust