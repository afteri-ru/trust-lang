#pragma once

namespace trust {

// Forward declarations
class Program;
class FuncDecl;
class ParamDecl;
class VarDecl;
class AssignmentStmt;
class ReturnStmt;
class CallExpr;
class VarRef;
class IntLiteral;
class StringLiteral;
class ExprStmt;
class IfStmt;
class BinaryOp;
class BlockStmt;
class WhileStmt;
class DoWhileStmt;
class WhileElseBlock;
class BreakStmt;
class ContinueStmt;
class TryCatchStmt;
class CatchBlock;
class ThrowStmt;
class MatchingStmt;
class MatchingCase;
class EnumDecl;
class EnumMember;
class StructDecl;
class StructField;
class EnumLiteral;
class MemberAccess;
class ArrayAccess;
class ArrayInit;
class CastExpr;
class RefMakeExpr;
class RefTakeExpr;

struct AstVisitor {
    virtual ~AstVisitor() = default;

    virtual void visit(const Program *node) = 0;
    virtual void visit(const FuncDecl *node) = 0;
    virtual void visit(const VarDecl *node) = 0;
    virtual void visit(const AssignmentStmt *node) = 0;
    virtual void visit(const ReturnStmt *node) = 0;
    virtual void visit(const CallExpr *node) = 0;
    virtual void visit(const VarRef *node) = 0;
    virtual void visit(const IntLiteral *node) = 0;
    virtual void visit(const StringLiteral *node) = 0;
    virtual void visit(const ExprStmt *node) = 0;
    virtual void visit(const IfStmt *node) = 0;
    virtual void visit(const WhileStmt *node) = 0;
    virtual void visit(const DoWhileStmt *node) = 0;
    virtual void visit(const WhileElseBlock *node) = 0;
    virtual void visit(const BreakStmt *node) = 0;
    virtual void visit(const ContinueStmt *node) = 0;
    virtual void visit(const TryCatchStmt *node) = 0;
    virtual void visit(const BinaryOp *node) = 0;
    virtual void visit(const BlockStmt *node) = 0;

    // Not a visitable AST node, but useful for generators
    virtual void visit(const ParamDecl *node) = 0;
    virtual void visit(const CatchBlock *node) = 0;
    virtual void visit(const ThrowStmt *node) = 0;
    virtual void visit(const MatchingStmt *node) = 0;
    virtual void visit(const MatchingCase *node) = 0;

    // enum and struct support
    virtual void visit(const EnumDecl *node) = 0;
    virtual void visit(const EnumMember *node) = 0;
    virtual void visit(const StructDecl *node) = 0;
    virtual void visit(const StructField *node) = 0;
    virtual void visit(const EnumLiteral *node) = 0;
    virtual void visit(const MemberAccess *node) = 0;
    virtual void visit(const ArrayAccess *node) = 0;
    virtual void visit(const ArrayInit *node) = 0;
    virtual void visit(const CastExpr *node) = 0;
    virtual void visit(const RefMakeExpr *node) = 0;
    virtual void visit(const RefTakeExpr *node) = 0;
};
} // namespace trust