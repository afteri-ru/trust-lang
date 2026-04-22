#pragma once

#include "gencpp/ast.hpp"
#include "gencpp/ast_visitor.hpp"
#include "gencpp/symbol_table.hpp"
#include "diag/context.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace trust {

// SemanticAnalyzer: traverse AST and fill TypeResolution with expression types.
// Must be run before Code Generation to populate type information.
// Errors are reported via DiagnosticEngine so analysis can continue.
class SemanticAnalyzer : public AstVisitor {
  public:
    SemanticAnalyzer(TypeResolution &type_res, SymbolTable &sym_table, Context &ctx) : type_res_(type_res), sym_table_(sym_table), ctx_(ctx) {}

    void analyze(const Program *program);

    // Check errors via diagnostic engine
    bool has_errors() const { return ctx_.diag().errorCount() > 0; }

    // --- AstVisitor interface ---
    void visit(const Program *node) override;
    void visit(const FuncDecl *node) override;
    void visit(const ParamDecl *node) override;
    void visit(const VarDecl *node) override;
    void visit(const AssignmentStmt *node) override;
    void visit(const ReturnStmt *node) override;
    void visit(const CallExpr *node) override;
    void visit(const ExprStmt *node) override;
    void visit(const IfStmt *node) override;
    void visit(const WhileStmt *node) override;
    void visit(const DoWhileStmt *node) override;
    void visit(const TryCatchStmt *node) override;
    void visit(const CatchBlock *node) override;
    void visit(const ThrowStmt *node) override;
    void visit(const MatchingStmt *node) override;
    void visit(const MatchingCase *node) override;
    void visit(const VarRef *node) override;
    void visit(const IntLiteral *node) override;
    void visit(const StringLiteral *node) override;
    void visit(const BinaryOp *node) override;
    void visit(const BlockStmt *node) override;
    void visit(const EnumDecl *node) override;
    void visit(const EnumMember *node) override;
    void visit(const StructDecl *node) override;
    void visit(const StructField *node) override;
    void visit(const EnumLiteral *node) override;
    void visit(const MemberAccess *node) override;
    void visit(const ArrayAccess *node) override;
    void visit(const ArrayInit *node) override;
    void visit(const CastExpr *node) override;
    void visit(const RefMakeExpr *node) override;
    void visit(const RefTakeExpr *node) override;
    void visit(const EmbedExpr *node) override;
    void visit(const WhileElseBlock *node) override;
    void visit(const BreakStmt *node) override;
    void visit(const ContinueStmt *node) override;

  private:
    TypeResolution &type_res_;
    SymbolTable &sym_table_;
    Context &ctx_;

    void dispatch_block(const BlockBody &body);
    void dispatch_block_item(const BlockItem &item);
    void report_error(SourceLoc loc, const std::string &msg);
};
} // namespace trust