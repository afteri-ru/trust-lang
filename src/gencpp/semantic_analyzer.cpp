#include "gencpp/semantic_analyzer.hpp"
#include <stdexcept>
#include <sstream>

using namespace trust;

void SemanticAnalyzer::analyze(const Program* program) {
    if (!program) return;

    // --- Pass 1: Collect top-level declarations ---
    for (const auto& item : program->items) {
        switch (item->token_kind()) {
            case ParserToken::Kind::FuncDecl: {
                auto* fd = static_cast<const FuncDecl*>(item.get());
                FuncSignature sig{fd->return_type, {}};
                for (const auto& p : fd->params) {
                    sig.param_types.push_back(p->param_type);
                }
                sym_table_.declare_function(fd->name, sig);
                break;
            }
            case ParserToken::Kind::EnumDecl: {
                auto* ed = static_cast<const EnumDecl*>(item.get());
                (void)ed;
                break;
            }
            case ParserToken::Kind::StructDecl: {
                auto* sd = static_cast<const StructDecl*>(item.get());
                (void)sd;
                break;
            }
            default:
                break;
        }
    }

    // --- Pass 2: Visit all items (resolve expression types) ---
    for (const auto& item : program->items) {
        item->accept(this);
    }
}

void SemanticAnalyzer::dispatch_block(const BlockBody& body) {
    for (const auto& item : body) {
        dispatch_block_item(item);
    }
}

void SemanticAnalyzer::dispatch_block_item(const BlockItem& item) {
    if (!item) return;
    item->accept(this);
}

// --- Visit implementations ---

void SemanticAnalyzer::visit(const Program* node) {
    for (const auto& item : node->items) {
        item->accept(this);
    }
}

void SemanticAnalyzer::visit(const FuncDecl* node) {
    // Enter new scope for function body
    sym_table_.push_scope();

    // Declare parameters in local scope
    for (const auto& p : node->params) {
        sym_table_.declare_var(p->name, p->param_type);
    }

    // Visit body
    if (node->body) {
        dispatch_block(node->body->body);
    }

    // Exit scope
    sym_table_.pop_scope();
}

void SemanticAnalyzer::visit(const ParamDecl* node) {
    (void)node;  // Parameters handled in FuncDecl visit
}

void SemanticAnalyzer::visit(const VarDecl* node) {
    TypeInfo resolved_type;

    if (node->needs_type_inference() && node->init) {
        // Type not explicitly specified — infer from initializer
        node->init->accept(this);
        auto init_type = type_res_.get_type(node->init.get());
        if (init_type.has_value()) {
            resolved_type = init_type.value();
        } else {
            // Could not determine type of initializer — report error
            report_error(node->loc, "Cannot infer type for variable '" + node->name +
                          "' from initializer expression");
            resolved_type = TypeInfo::builtin(TypeKind::Int); // fallback
        }
    } else {
        // Type explicitly specified — use it
        resolved_type = node->var_type;
        if (node->init) {
            node->init->accept(this);
        }
    }

    // Declare variable in current scope
    sym_table_.declare_var(node->name, resolved_type);
}

void SemanticAnalyzer::visit(const AssignmentStmt* node) {
    // Visit value first to resolve its type
    if (node->value) {
        node->value->accept(this);
    }

    // Visit target expression
    if (node->target) {
        node->target->accept(this);
    }

    // Type checking for simple variable assignment
    if (node->target) {
        if (auto* var_ref = dynamic_cast<const VarRef*>(node->target.get())) {
            auto expr_type = type_res_.get_type(node->value.get());
            if (!expr_type.has_value()) {
                report_error(node->loc, "Cannot determine type of assignment value");
                return;
            }
            auto result = sym_table_.check_assignment(var_ref->name, expr_type.value(), var_ref->loc);
            if (!result.has_value()) {
                report_error(var_ref->loc, result.error());
            }
        }
    }
}

void SemanticAnalyzer::visit(const ReturnStmt* node) {
    if (node->value) {
        node->value->accept(this);
    }
    // ReturnStmt is not an Expr, so we don't store its type in TypeResolution
}

void SemanticAnalyzer::visit(const CallExpr* node) {
    // Visit arguments
    for (const auto& arg : node->args) {
        if (arg) arg->accept(this);
    }

    // Resolve function return type from SymbolTable
    if (const FuncSignature* sig = sym_table_.lookup_function(node->name)) {
        type_res_.set_type(node, sig->return_type);
    } else {
        // Unknown function — could be a builtin like print
        if (node->name == "print") {
            type_res_.set_type(node, TypeInfo::builtin(TypeKind::Void));
        } else {
            report_error(node->loc, "Unknown function '" + node->name + "'");
            type_res_.set_type(node, TypeInfo::builtin(TypeKind::Void));
        }
    }
}

void SemanticAnalyzer::visit(const ExprStmt* node) {
    if (node->expr) {
        node->expr->accept(this);
    }
}

void SemanticAnalyzer::visit(const IfStmt* node) {
    if (node->condition) {
        node->condition->accept(this);
        type_res_.set_type(node->condition.get(), TypeInfo::builtin(TypeKind::Bool));
    }

    // Enter new scope for then block
    sym_table_.push_scope();
    dispatch_block(node->then_body);
    sym_table_.pop_scope();

    if (node->else_if) {
        node->else_if->accept(this);
    } else if (node->else_block) {
        sym_table_.push_scope();
        dispatch_block(node->else_block->body);
        sym_table_.pop_scope();
    }
}

void SemanticAnalyzer::visit(const WhileStmt* node) {
    if (node->condition) {
        node->condition->accept(this);
    }

    sym_table_.push_scope();
    dispatch_block(node->body);
    sym_table_.pop_scope();

    if (!node->else_body.empty()) {
        sym_table_.push_scope();
        dispatch_block(node->else_body);
        sym_table_.pop_scope();
    }
}

void SemanticAnalyzer::visit(const DoWhileStmt* node) {
    sym_table_.push_scope();
    dispatch_block(node->body);
    sym_table_.pop_scope();

    if (node->condition) {
        node->condition->accept(this);
    }
}

void SemanticAnalyzer::visit(const WhileElseBlock* node) {
    sym_table_.push_scope();
    dispatch_block(node->body);
    sym_table_.pop_scope();
}

void SemanticAnalyzer::visit(const BreakStmt* node) {
    (void)node;
    // Break is valid anywhere a loop or switch could be
    // Contextual validation happens during codegen
}

void SemanticAnalyzer::visit(const ContinueStmt* node) {
    (void)node;
    // Continue is valid only within loops
    // Contextual validation happens during codegen
}

void SemanticAnalyzer::visit(const TryCatchStmt* node) {
    sym_table_.push_scope();
    dispatch_block(node->try_body);
    sym_table_.pop_scope();

    if (node->catch_block) {
        sym_table_.push_scope();
        // Declare catch variable
        sym_table_.declare_var(node->catch_block->var_name, node->catch_block->exception_type);
        dispatch_block(node->catch_block->body);
        sym_table_.pop_scope();
    }
}

void SemanticAnalyzer::visit(const CatchBlock* node) {
    // Block-level dispatch handled in TryCatchStmt
    (void)node;
}

void SemanticAnalyzer::visit(const ThrowStmt* node) {
    if (node->value) {
        node->value->accept(this);
    }
}

void SemanticAnalyzer::visit(const MatchingStmt* node) {
    if (node->expression) {
        node->expression->accept(this);
    }

    for (const auto& c : node->cases) {
        if (c) c->accept(this);
    }

    for (const auto& item : node->else_body) {
        dispatch_block_item(item);
    }
}

void SemanticAnalyzer::visit(const MatchingCase* node) {
    if (node->pattern) {
        node->pattern->accept(this);
    }
    for (const auto& item : node->body) {
        dispatch_block_item(item);
    }
}

void SemanticAnalyzer::visit(const VarRef* node) {
    // Resolve type from SymbolTable — report error if unknown
    auto result = sym_table_.lookup_var(node->name, node->loc);
    if (!result.has_value()) {
        report_error(node->loc, result.error());
        type_res_.set_type(node, TypeInfo::builtin(TypeKind::Int)); // fallback for continued analysis
        return;
    }
    type_res_.set_type(node, result.value());
}

void SemanticAnalyzer::visit(const IntLiteral* node) {
    type_res_.set_type(node, TypeInfo::builtin(TypeKind::Int));
}

void SemanticAnalyzer::visit(const StringLiteral* node) {
    type_res_.set_type(node, TypeInfo::builtin(TypeKind::String));
}

void SemanticAnalyzer::visit(const BinaryOp* node) {
    // Visit operands first
    if (node->left) node->left->accept(this);
    if (node->right) node->right->accept(this);

    // Resolve type based on operator
    TypeInfo result_type;
    switch (node->op) {
        case BinOp::Eq:
        case BinOp::Ne:
        case BinOp::Lt:
        case BinOp::Le:
        case BinOp::Gt:
        case BinOp::Ge:
        case BinOp::And:
        case BinOp::Or:
            result_type = TypeInfo::builtin(TypeKind::Bool);
            break;
        default:
            // Arithmetic: use left operand's type
            {
                auto left_type = type_res_.get_type(node->left.get());
                if (left_type.has_value()) {
                    result_type = left_type.value();
                } else {
                    report_error(node->loc, "Cannot determine type of left operand in binary expression");
                    result_type = TypeInfo::builtin(TypeKind::Int); // fallback
                }
            }
            break;
    }
    type_res_.set_type(node, result_type);
}

void SemanticAnalyzer::visit(const BlockStmt* node) {
    sym_table_.push_scope();
    dispatch_block(node->body);
    sym_table_.pop_scope();
}

void SemanticAnalyzer::visit(const EnumDecl* node) {
    // Enum types are resolved as UserType by name
    (void)node;
}

void SemanticAnalyzer::visit(const EnumMember* node) {
    (void)node;
}

void SemanticAnalyzer::visit(const StructDecl* node) {
    // Struct types are resolved as UserType by name
    for (const auto& field : node->fields) {
        // Visit default init expression if any
        if (field->init) {
            field->init->accept(this);
        }
    }
    // Visit methods
    for (const auto& method : node->methods) {
        method->accept(this);
    }
}

void SemanticAnalyzer::visit(const StructField* node) {
    (void)node;  // Fields handled in StructDecl
}

void SemanticAnalyzer::visit(const EnumLiteral* node) {
    // Type is the enum type (UserType)
    type_res_.set_type(node, TypeInfo::user(node->enum_name));
}

void SemanticAnalyzer::visit(const MemberAccess* node) {
    if (node->object) {
        node->object->accept(this);
    }
    // Type resolution for field access requires StructDecl info
    // For now, default to int (would need struct field type lookup)
    type_res_.set_type(node, TypeInfo::builtin(TypeKind::Int));
}

void SemanticAnalyzer::visit(const ArrayAccess* node) {
    if (node->array) node->array->accept(this);
    if (node->index) node->index->accept(this);

    // Array element access: for torch::Tensor arrays, element type is inferred
    type_res_.set_type(node, TypeInfo::builtin(TypeKind::Int));
}

void SemanticAnalyzer::visit(const ArrayInit* node) {
    // Visit elements
    for (const auto& elem : node->elements) {
        if (elem) elem->accept(this);
    }
    // ArrayInit itself has a tensor type
    type_res_.set_type(node, TypeInfo::builtin(TypeKind::Int));
}

void SemanticAnalyzer::visit(const CastExpr* node) {
    if (node->expr) {
        node->expr->accept(this);
    }
    type_res_.set_type(node, node->target_type);
}

void SemanticAnalyzer::visit(const RefMakeExpr* node) {
    if (node->arg) {
        node->arg->accept(this);
    }
    // Reference has same type as argument
    auto arg_type = type_res_.get_type(node->arg.get());
    if (arg_type.has_value()) {
        type_res_.set_type(node, arg_type.value());
    }
}

void SemanticAnalyzer::visit(const RefTakeExpr* node) {
    if (node->arg) {
        node->arg->accept(this);
    }
    // Dereferencing returns the pointed-to type
    auto arg_type = type_res_.get_type(node->arg.get());
    if (arg_type.has_value()) {
        type_res_.set_type(node, arg_type.value());
    }
}

void SemanticAnalyzer::report_error(SourceLoc loc, const std::string& msg) {
    ctx_.diag().report(loc, Severity::Error, "{}", msg);
}