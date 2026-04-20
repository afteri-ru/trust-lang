#pragma once

#include "gencpp/ast.hpp"
#include "gencpp/ast_visitor.hpp"
#include <stdexcept>
#include <string>
#include <sstream>

namespace trust {

class AstValidator : public AstVisitor {
  public:
    void validate(const Program *program) { program->accept(this); }

  private:
    [[noreturn]] void error(const std::string &msg) { throw std::runtime_error("AST validation error: " + msg); }

    void validate_not_empty(const std::string &v, const std::string &field) {
        if (v.empty())
            error(field + " must not be empty");
    }

    void validate_not_null(const void *ptr, const std::string &field) {
        if (!ptr)
            error(field + " must not be null");
    }

    void dispatch_block_item(const BlockItem &item) {
        if (!item) return;
        // BlockItem is unique_ptr<AstVisitable> — use visitor pattern directly
        item->accept(this);
    }

    void visit(const Program *node) override {
        if (node->items.empty())
            error("Program must have at least one item");
        for (const auto &item : node->items) {
            validate_not_null(item.get(), "Program item");
            item->accept(this);
        }
    }

    void visit(const FuncDecl *node) override {
        validate_not_empty(node->name, "FuncDecl::name");
        for (const auto &p : node->params) {
            validate_not_null(p.get(), "ParamDecl");
            validate_not_empty(p->name, "ParamDecl::name");
        }
        validate_not_null(node->body.get(), "FuncDecl::body");
        node->body->accept(this);
    }

    void visit(const ParamDecl *node) override { validate_not_empty(node->name, "ParamDecl::name"); }

    void visit(const VarDecl *node) override {
        validate_not_empty(node->name, "VarDecl::name");
        if (node->init)
            node->init->accept(this);
    }

    void visit(const AssignmentStmt *node) override {
        validate_not_null(node->target.get(), "AssignmentStmt::target");
        validate_not_null(node->value.get(), "AssignmentStmt::value");
        node->target->accept(this);
        node->value->accept(this);
    }

    void visit(const ReturnStmt *node) override {
        if (node->value)
            node->value->accept(this);
    }

    void visit(const CallExpr *node) override {
        validate_not_empty(node->name, "CallExpr::name");
        for (const auto &arg : node->args) {
            validate_not_null(arg.get(), "CallExpr::arg");
            arg->accept(this);
        }
    }

    void visit(const ExprStmt *node) override {
        validate_not_null(node->expr.get(), "ExprStmt::expr");
        node->expr->accept(this);
    }

    void visit(const IfStmt *node) override {
        validate_not_null(node->condition.get(), "IfStmt::condition");
        node->condition->accept(this);
        for (const auto &s : node->then_body)
            dispatch_block_item(s);
        if (node->else_if) {
            node->else_if->accept(this);
        } else if (node->else_block) {
            node->else_block->accept(this);
        }
    }

    void visit(const BinaryOp *node) override {
        validate_not_null(node->left.get(), "BinaryOp::left");
        validate_not_null(node->right.get(), "BinaryOp::right");
        node->left->accept(this);
        node->right->accept(this);
    }

    void visit(const VarRef *node) override { validate_not_empty(node->name, "VarRef::name"); }

    void visit(const IntLiteral *node) override {}
    void visit(const StringLiteral *node) override {}

    void visit(const BlockStmt *node) override {
        for (const auto &stmt : node->body)
            dispatch_block_item(stmt);
    }

    void visit(const DoWhileStmt *node) override {
        for (const auto &item : node->body)
            dispatch_block_item(item);
        if (node->condition)
            node->condition->accept(this);
    }

    void visit(const TryCatchStmt *node) override {
        for (const auto &item : node->try_body)
            dispatch_block_item(item);
        if (node->catch_block)
            visit(node->catch_block.get());
    }

    void visit(const CatchBlock *node) override {
        validate_not_empty(node->var_name, "CatchBlock::var_name");
        for (const auto &item : node->body)
            dispatch_block_item(item);
    }

    void visit(const ThrowStmt *node) override {
        validate_not_null(node->value.get(), "ThrowStmt::value");
        node->value->accept(this);
    }

    void visit(const MatchingStmt *node) override {
        validate_not_null(node->expression.get(), "MatchingStmt::expression");
        node->expression->accept(this);
        for (const auto &c : node->cases) {
            validate_not_null(c.get(), "MatchingStmt::case");
            if (c->pattern)
                c->pattern->accept(this);
            for (const auto &item : c->body)
                dispatch_block_item(item);
        }
        for (const auto &item : node->else_body)
            dispatch_block_item(item);
    }

    void visit(const MatchingCase *node) override {
        if (node->pattern)
            node->pattern->accept(this);
        for (const auto &item : node->body)
            dispatch_block_item(item);
    }

    void visit(const EnumDecl *node) override { validate_not_empty(node->name, "EnumDecl::name"); }

    void visit(const StructDecl *node) override { validate_not_empty(node->name, "StructDecl::name"); }

    void visit(const EnumLiteral *node) override {
        validate_not_empty(node->enum_name, "EnumLiteral::enum_name");
        validate_not_empty(node->member_name, "EnumLiteral::member_name");
    }

    void visit(const MemberAccess *node) override {
        validate_not_null(node->object.get(), "MemberAccess::object");
        validate_not_empty(node->field, "MemberAccess::field");
        node->object->accept(this);
    }

    void visit(const ArrayAccess *node) override {
        validate_not_null(node->array.get(), "ArrayAccess::array");
        validate_not_null(node->index.get(), "ArrayAccess::index");
        node->array->accept(this);
        node->index->accept(this);
    }

    void visit(const ArrayInit *node) override {
        for (const auto &elem : node->elements) {
            validate_not_null(elem.get(), "ArrayInit::element");
            elem->accept(this);
        }
    }

    void visit(const CastExpr *node) override {
        validate_not_null(node->expr.get(), "CastExpr::expr");
        node->expr->accept(this);
    }

    void visit(const EnumMember *node) override { validate_not_empty(node->name, "EnumMember::name"); }

    void visit(const StructField *node) override {
        validate_not_empty(node->name, "StructField::name");
    }

    void visit(const RefMakeExpr *node) override {
        validate_not_null(node->arg.get(), "RefMakeExpr::arg");
        node->arg->accept(this);
    }

    void visit(const RefTakeExpr *node) override {
        validate_not_null(node->arg.get(), "RefTakeExpr::arg");
        node->arg->accept(this);
    }

    void visit(const WhileElseBlock *node) override {
        for (const auto &item : node->body)
            dispatch_block_item(item);
    }

    void visit(const BreakStmt *node) override { (void)node; }

    void visit(const ContinueStmt *node) override { (void)node; }

    void visit(const WhileStmt *node) override {
        if (node->condition)
            node->condition->accept(this);
        for (const auto &item : node->body)
            dispatch_block_item(item);
        for (const auto &item : node->else_body)
            dispatch_block_item(item);
    }
};

[[nodiscard]] inline bool trans_validate(const Program *program) {
    if (!program)
        return false;
    try {
        AstValidator validator;
        validator.validate(program);
        return true;
    } catch (const std::exception &e) {
        return false;
    }
}
} // namespace trust