#pragma once

#include "gencpp/ast.hpp"
#include "gencpp/ast_visitor.hpp"
#include <stdexcept>
#include <string>

namespace trust {

class AstValidator;

// FieldValidation trait — each specialization lists required fields for a type
template <typename T>
struct FieldValidation {
    static void check(const T *, AstValidator &) {}
};

class AstValidator : public AstVisitor {
  public:
    void validate(const Program *program) {
        if (!program)
            return;
        program->accept(this);
    }

    // All FieldValidation specializations can access private helpers
#define FIELD_VALIDATION_FRIENDS                   \
    friend struct FieldValidation<Program>;        \
    friend struct FieldValidation<FuncDecl>;       \
    friend struct FieldValidation<ParamDecl>;      \
    friend struct FieldValidation<VarDecl>;        \
    friend struct FieldValidation<AssignmentStmt>; \
    friend struct FieldValidation<CallExpr>;       \
    friend struct FieldValidation<ExprStmt>;       \
    friend struct FieldValidation<IfStmt>;         \
    friend struct FieldValidation<BinaryOp>;       \
    friend struct FieldValidation<VarRef>;         \
    friend struct FieldValidation<CatchBlock>;     \
    friend struct FieldValidation<ThrowStmt>;      \
    friend struct FieldValidation<MatchingStmt>;   \
    friend struct FieldValidation<EnumDecl>;       \
    friend struct FieldValidation<StructDecl>;     \
    friend struct FieldValidation<EnumLiteral>;    \
    friend struct FieldValidation<MemberAccess>;   \
    friend struct FieldValidation<ArrayAccess>;    \
    friend struct FieldValidation<ArrayInit>;      \
    friend struct FieldValidation<CastExpr>;       \
    friend struct FieldValidation<EnumMember>;     \
    friend struct FieldValidation<StructField>;    \
    friend struct FieldValidation<RefMakeExpr>;    \
    friend struct FieldValidation<RefTakeExpr>;

    FIELD_VALIDATION_FRIENDS

  private:
    [[noreturn]] void error(const std::string &msg) { throw std::runtime_error("AST validation error: " + msg); }

    template <typename NodeType>
    void require_name(const NodeType *node) {
        if (node->name.empty())
            error(AstTypeTraits::to_string(NodeType::static_token_kind()) + "::name must not be empty");
    }

    template <typename NodeType>
    void require_name_typed(const NodeType *node, const char *field) {
        if (node) {
            // placeholder to check field existence at compile time
        }
        (void)node;
        (void)field;
    }

    void check_null(const void *ptr, const char *type_name, const char *field) {
        if (!ptr)
            error(std::string(type_name) + "::" + field + " must not be null");
    }

    void check_empty(const std::string &s, const char *type_name, const char *field) {
        if (s.empty())
            error(std::string(type_name) + "::" + field + " must not be empty");
    }

    template <typename NodeType>
    void validate_fields(const NodeType *node) {
        FieldValidation<NodeType>::check(node, *this);
    }

    void dispatch_block_item(const BlockItem &item) {
        if (item)
            item->accept(this);
    }

    void visit(const Program *node) override {
        validate_fields(node);
        for (const auto &item : node->items)
            item->accept(this);
    }

    void visit(const FuncDecl *node) override {
        validate_fields(node);
        for (const auto &p : node->params) {
            if (p)
                p->accept(this);
        }
        node->body->accept(this);
    }

    void visit(const ParamDecl *node) override { validate_fields(node); }

    void visit(const VarDecl *node) override {
        validate_fields(node);
        if (node->init)
            node->init->accept(this);
    }

    void visit(const AssignmentStmt *node) override {
        validate_fields(node);
        node->target->accept(this);
        node->value->accept(this);
    }

    void visit(const ReturnStmt *node) override {
        if (node->value)
            node->value->accept(this);
    }

    void visit(const CallExpr *node) override {
        validate_fields(node);
        for (const auto &arg : node->args)
            arg->accept(this);
    }

    void visit(const ExprStmt *node) override {
        validate_fields(node);
        node->expr->accept(this);
    }

    void visit(const IfStmt *node) override {
        validate_fields(node);
        node->condition->accept(this);
        for (const auto &s : node->then_body)
            dispatch_block_item(s);
        if (node->else_if)
            node->else_if->accept(this);
        else if (node->else_block)
            node->else_block->accept(this);
    }

    void visit(const BinaryOp *node) override {
        validate_fields(node);
        node->left->accept(this);
        node->right->accept(this);
    }

    void visit(const VarRef *node) override { validate_fields(node); }

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
        validate_fields(node);
        for (const auto &item : node->body)
            dispatch_block_item(item);
    }

    void visit(const ThrowStmt *node) override {
        validate_fields(node);
        node->value->accept(this);
    }

    void visit(const MatchingStmt *node) override {
        validate_fields(node);
        node->expression->accept(this);
        for (const auto &c : node->cases) {
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

    void visit(const EnumDecl *node) override { validate_fields(node); }

    void visit(const StructDecl *node) override { validate_fields(node); }

    void visit(const EnumLiteral *node) override { validate_fields(node); }

    void visit(const MemberAccess *node) override {
        validate_fields(node);
        node->object->accept(this);
    }

    void visit(const ArrayAccess *node) override {
        validate_fields(node);
        node->array->accept(this);
        node->index->accept(this);
    }

    void visit(const ArrayInit *node) override {
        validate_fields(node);
        for (const auto &elem : node->elements)
            elem->accept(this);
    }

    void visit(const CastExpr *node) override {
        validate_fields(node);
        node->expr->accept(this);
    }

    void visit(const EnumMember *node) override { validate_fields(node); }

    void visit(const StructField *node) override { validate_fields(node); }

    void visit(const RefMakeExpr *node) override {
        validate_fields(node);
        node->arg->accept(this);
    }

    void visit(const RefTakeExpr *node) override {
        validate_fields(node);
        node->arg->accept(this);
    }

    void visit(const EmbedExpr *node) override {}

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

// Validation macros — used by FieldValidation specializations
#define REQ_NAME(NODE) require_name(NODE)
#define REQ_NULL(PTR, NT, F) check_null(PTR, AstTypeTraits::to_string(NT::static_token_kind()).c_str(), F)
#define REQ_EMPTY(S, NT, F) check_empty(S, AstTypeTraits::to_string(NT::static_token_kind()).c_str(), F)

// === FieldValidation specializations ===

template <>
struct FieldValidation<Program> {
    static void check(const Program *n, AstValidator &v) {
        if (n->items.empty())
            v.error("Program must have at least one item");
        for (const auto &item : n->items)
            if (!item)
                v.error("Program item must not be null");
    }
};

template <>
struct FieldValidation<FuncDecl> {
    static void check(const FuncDecl *n, AstValidator &v) {
        v.REQ_NAME(n);
        for (const auto &p : n->params) {
            if (!p)
                v.REQ_NULL(p.get(), FuncDecl, "param");
            else
                v.REQ_NAME(p.get());
        }
        v.REQ_NULL(n->body.get(), FuncDecl, "body");
    }
};

template <>
struct FieldValidation<ParamDecl> {
    static void check(const ParamDecl *n, AstValidator &v) { v.REQ_NAME(n); }
};

template <>
struct FieldValidation<VarDecl> {
    static void check(const VarDecl *n, AstValidator &v) { v.REQ_NAME(n); }
};

template <>
struct FieldValidation<AssignmentStmt> {
    static void check(const AssignmentStmt *n, AstValidator &v) {
        v.REQ_NULL(n->target.get(), AssignmentStmt, "target");
        v.REQ_NULL(n->value.get(), AssignmentStmt, "value");
    }
};

template <>
struct FieldValidation<CallExpr> {
    static void check(const CallExpr *n, AstValidator &v) {
        v.REQ_NAME(n);
        for (const auto &arg : n->args)
            v.REQ_NULL(arg.get(), CallExpr, "arg");
    }
};

template <>
struct FieldValidation<ExprStmt> {
    static void check(const ExprStmt *n, AstValidator &v) { v.REQ_NULL(n->expr.get(), ExprStmt, "expr"); }
};

template <>
struct FieldValidation<IfStmt> {
    static void check(const IfStmt *n, AstValidator &v) { v.REQ_NULL(n->condition.get(), IfStmt, "condition"); }
};

template <>
struct FieldValidation<BinaryOp> {
    static void check(const BinaryOp *n, AstValidator &v) {
        v.REQ_NULL(n->left.get(), BinaryOp, "left");
        v.REQ_NULL(n->right.get(), BinaryOp, "right");
    }
};

template <>
struct FieldValidation<VarRef> {
    static void check(const VarRef *n, AstValidator &v) { v.REQ_NAME(n); }
};

template <>
struct FieldValidation<CatchBlock> {
    static void check(const CatchBlock *n, AstValidator &v) { v.REQ_EMPTY(n->var_name, CatchBlock, "var_name"); }
};

template <>
struct FieldValidation<ThrowStmt> {
    static void check(const ThrowStmt *n, AstValidator &v) { v.REQ_NULL(n->value.get(), ThrowStmt, "value"); }
};

template <>
struct FieldValidation<MatchingStmt> {
    static void check(const MatchingStmt *n, AstValidator &v) {
        v.REQ_NULL(n->expression.get(), MatchingStmt, "expression");
        for (const auto &c : n->cases)
            v.REQ_NULL(c.get(), MatchingStmt, "case");
    }
};

template <>
struct FieldValidation<EnumDecl> {
    static void check(const EnumDecl *n, AstValidator &v) { v.REQ_NAME(n); }
};

template <>
struct FieldValidation<StructDecl> {
    static void check(const StructDecl *n, AstValidator &v) { v.REQ_NAME(n); }
};

template <>
struct FieldValidation<EnumLiteral> {
    static void check(const EnumLiteral *n, AstValidator &v) {
        v.REQ_EMPTY(n->enum_name, EnumLiteral, "enum_name");
        v.REQ_EMPTY(n->member_name, EnumLiteral, "member_name");
    }
};

template <>
struct FieldValidation<MemberAccess> {
    static void check(const MemberAccess *n, AstValidator &v) {
        v.REQ_NULL(n->object.get(), MemberAccess, "object");
        v.REQ_EMPTY(n->field, MemberAccess, "field");
    }
};

template <>
struct FieldValidation<ArrayAccess> {
    static void check(const ArrayAccess *n, AstValidator &v) {
        v.REQ_NULL(n->array.get(), ArrayAccess, "array");
        v.REQ_NULL(n->index.get(), ArrayAccess, "index");
    }
};

template <>
struct FieldValidation<ArrayInit> {
    static void check(const ArrayInit *n, AstValidator &v) {
        for (const auto &elem : n->elements)
            v.REQ_NULL(elem.get(), ArrayInit, "element");
    }
};

template <>
struct FieldValidation<CastExpr> {
    static void check(const CastExpr *n, AstValidator &v) { v.REQ_NULL(n->expr.get(), CastExpr, "expr"); }
};

template <>
struct FieldValidation<EnumMember> {
    static void check(const EnumMember *n, AstValidator &v) { v.REQ_NAME(n); }
};

template <>
struct FieldValidation<StructField> {
    static void check(const StructField *n, AstValidator &v) { v.REQ_NAME(n); }
};

template <>
struct FieldValidation<RefMakeExpr> {
    static void check(const RefMakeExpr *n, AstValidator &v) { v.REQ_NULL(n->arg.get(), RefMakeExpr, "arg"); }
};

template <>
struct FieldValidation<RefTakeExpr> {
    static void check(const RefTakeExpr *n, AstValidator &v) { v.REQ_NULL(n->arg.get(), RefTakeExpr, "arg"); }
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