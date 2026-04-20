#include "gencpp/ast_printer.hpp"
#include "gencpp/ast.hpp"
#include <string>
#include <vector>
#include <utility>

namespace trust {

// AST Printer visitor — serializes AST back to text format
struct AstPrinter : public AstVisitor {
    std::string output;
    int indent = 0;

    void emit(const char* type, const std::vector<std::pair<std::string, std::string>>& attrs) {
        output += std::string(indent * 2, ' ') + type;
        for (auto& [k, v] : attrs) output += " " + k + "=" + v;
        output += "\n";
    }
    void up() { indent++; }
    void down() { if (indent > 0) indent--; }

    void dispatch_block_item(const BlockItem& item) {
        if (!item) return;
        item->accept(this);
    }

    void visit(const IntLiteral* n) override { emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"value", std::to_string(n->value)}}); }
    void visit(const StringLiteral* n) override { emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"value", "\"" + StringUtils::escape(n->value) + "\""}}); }
    void visit(const VarRef* n) override { emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}}); }
    void visit(const BinaryOp* n) override {
        const char* op_str = nullptr;
        switch (n->op) {
            case BinOp::Add: op_str = "+"; break; case BinOp::Sub: op_str = "-"; break;
            case BinOp::Mul: op_str = "*"; break; case BinOp::Div: op_str = "/"; break;
            case BinOp::Eq:  op_str = "=="; break; case BinOp::Ne:  op_str = "!="; break;
            case BinOp::Lt:  op_str = "<"; break; case BinOp::Le:  op_str = "<="; break;
            case BinOp::Ge:  op_str = ">="; break; case BinOp::Gt: op_str = ">"; break;
            case BinOp::And: op_str = "and"; break; case BinOp::Or:  op_str = "or"; break;
        }
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"op", op_str}});
        up(); n->left->accept(this); n->right->accept(this); down();
    }
    void visit(const CallExpr* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}});
        up(); for (auto& a : n->args) if (a) a->accept(this); down();
    }
    void visit(const VarDecl* n) override {
            emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}, {"type", n->type_info().to_string()}, {"inferred", n->needs_type_inference() ? "true" : "false"}});
        up(); if (n->init) n->init->accept(this); down();
    }
    void visit(const AssignmentStmt* n) override {
        // Print target
        std::string target_str;
        if (auto* vr = dynamic_cast<const VarRef*>(n->target.get())) {
            target_str = vr->name;
        } else {
            // For complex targets, we print a placeholder
            target_str = "<expr>";
        }
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"target", target_str}});
        up();
        if (n->target && !dynamic_cast<const VarRef*>(n->target.get())) {
            n->target->accept(this);
        }
        if (n->value) n->value->accept(this);
        down();
    }
    void visit(const ReturnStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up(); if (n->value) n->value->accept(this); down();
    }
    void visit(const ExprStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up(); if (n->expr) n->expr->accept(this); down();
    }
    void visit(const BlockStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up(); for (auto& bitem : n->body) dispatch_block_item(bitem); down();
    }
    void visit(const IfStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        if (n->condition) n->condition->accept(this);
        emit(AstTypeTraits::to_string(ParserToken::Kind::ThenBlock).c_str(), {});
        up(); for (auto& bitem : n->then_body) dispatch_block_item(bitem); down();
        if (n->else_if) {
            emit(AstTypeTraits::to_string(ParserToken::Kind::ElseBlock).c_str(), {});
            up(); n->else_if->accept(this); down();
        } else if (n->else_block) {
            emit(AstTypeTraits::to_string(ParserToken::Kind::ElseBlock).c_str(), {});
            up(); for (auto& bitem : n->else_block->body) dispatch_block_item(bitem); down();
        }
        down();
    }
    void visit(const ParamDecl* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}, {"type", n->param_type.to_string()}});
    }
    void visit(const WhileStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        if (n->condition) n->condition->accept(this);
        for (const auto& bitem : n->body) dispatch_block_item(bitem);
        if (!n->else_body.empty()) {
            emit(AstTypeTraits::to_string(ParserToken::Kind::WhileElseBlock).c_str(), {});
            up();
            for (const auto& bitem : n->else_body) dispatch_block_item(bitem);
            down();
        }
        down();
    }
    void visit(const DoWhileStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        for (const auto& bitem : n->body) dispatch_block_item(bitem);
        if (n->condition) n->condition->accept(this);
        down();
    }
    void visit(const TryCatchStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        for (const auto& bitem : n->try_body) dispatch_block_item(bitem);
        if (n->catch_block) this->visit(n->catch_block.get());
        down();
    }
    void visit(const ThrowStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        if (n->value) n->value->accept(this);
        down();
    }
    void visit(const MatchingStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        if (n->expression) n->expression->accept(this);
        for (const auto& c : n->cases) {
            emit(AstTypeTraits::to_string(ParserToken::Kind::MatchingCase).c_str(), {});
            up();
            if (c->pattern) c->pattern->accept(this);
            for (const auto& bitem : c->body) dispatch_block_item(bitem);
            down();
        }
        if (!n->else_body.empty()) {
            emit(AstTypeTraits::to_string(ParserToken::Kind::MatchingElseBlock).c_str(), {});
            up();
            for (const auto& bitem : n->else_body) dispatch_block_item(bitem);
            down();
        }
        down();
    }
    void visit(const MatchingCase* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        if (n->pattern) n->pattern->accept(this);
        for (const auto& bitem : n->body) dispatch_block_item(bitem);
        down();
    }
    void visit(const CatchBlock* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"type", n->exception_type.to_string()}, {"name", n->var_name}});
        up();
        for (const auto& bitem : n->body) dispatch_block_item(bitem);
        down();
    }
    void visit(const FuncDecl* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}, {"ret", n->return_type.to_string()}});
        up();
        for (auto& p : n->params) emit(AstTypeTraits::to_string(ParserToken::Kind::ParamDecl).c_str(), {{"name", p->name}, {"type", p->param_type.to_string()}});
        if (n->body) n->body->accept(this);
        down();
    }
    void visit(const Program* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        for (auto& item : n->items) {
            item->accept(this);
        }
        down();
    }
    void visit(const EnumDecl* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}});
        up();
        for (auto& m : n->members) {
            if (m->value) {
                emit(AstTypeTraits::to_string(ParserToken::Kind::EnumMember).c_str(), {{"name", m->name}, {"value", std::to_string(*m->value)}});
            } else {
                emit(AstTypeTraits::to_string(ParserToken::Kind::EnumMember).c_str(), {{"name", m->name}});
            }
        }
        down();
    }
    void visit(const EnumMember* n) override {
        if (n->value) {
            emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}, {"value", std::to_string(*n->value)}});
        } else {
            emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}});
        }
    }
    void visit(const StructDecl* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}});
        up();
        for (auto& f : n->fields) {
            if (f->init) {
                emit(AstTypeTraits::to_string(ParserToken::Kind::StructField).c_str(), {{"name", f->name}, {"type", f->type.to_string()}});
                up(); f->init->accept(this); down();
            } else {
                emit(AstTypeTraits::to_string(ParserToken::Kind::StructField).c_str(), {{"name", f->name}, {"type", f->type.to_string()}});
            }
        }
        for (auto& m : n->methods) {
            if (m) m->accept(this);
        }
        down();
    }
    void visit(const StructField* n) override {
        if (n->init) {
            emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}, {"type", n->type.to_string()}});
            up(); n->init->accept(this); down();
        } else {
            emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"name", n->name}, {"type", n->type.to_string()}});
        }
    }
    void visit(const EnumLiteral* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"enum", n->enum_name}, {"member", n->member_name}});
    }
    void visit(const MemberAccess* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"field", n->field}});
        up(); if (n->object) n->object->accept(this); down();
    }
    void visit(const ArrayAccess* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        if (n->array) n->array->accept(this);
        if (n->index) n->index->accept(this);
        down();
    }
    void visit(const ArrayInit* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"type", TypeInfo::builtin(n->element_type).to_string()}});
        up();
        for (auto& e : n->elements) if (e) e->accept(this);
        down();
    }
    void visit(const CastExpr* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {{"type", n->target_type.to_string()}});
        up();
        if (n->expr) n->expr->accept(this);
        down();
    }
    void visit(const RefMakeExpr* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        if (n->arg) n->arg->accept(this);
        down();
    }
    void visit(const RefTakeExpr* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        if (n->arg) n->arg->accept(this);
        down();
    }
    void visit(const WhileElseBlock* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
        up();
        for (const auto& bitem : n->body) dispatch_block_item(bitem);
        down();
    }
    void visit(const BreakStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
    }
    void visit(const ContinueStmt* n) override {
        emit(AstTypeTraits::to_string(n->token_kind()).c_str(), {});
    }
};

std::string print_ast(const Program* program) {
    if (!program) return "";
    AstPrinter printer;
    program->accept(&printer);
    return printer.output;
}

} // namespace trust