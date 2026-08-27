#pragma once

// include/transpiler/stmt_emit.hpp
// Компонент кодогенерации: StmtEmitter. Разделяет CppEmitContext с драйвером CppTranspiler;
// рекурсия/вызовы других компонентов идут через драйвер (friend).

#include "transpiler/emit_ctx.hpp"
#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "types/type_id.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class CppTranspiler;

class StmtEmitter {
  public:
    explicit StmtEmitter(CppEmitContext& ectx, CppTranspiler& driver)
    : m_ectx(ectx)
    , m_driver(driver) {}
    void emitBlockBodyToFile(const std::vector<AstNodePtr>& body, MapperRange blockRange, MapperFile output_idx, bool mapBlock,
                             const std::string& beforeCloseLabel, const std::string& afterOpen, const std::vector<AstNodePtr>* preTrust = nullptr,
                             const std::vector<AstNodePtr>* postTrust = nullptr);
    void generateIfToFile(const IfStmt& node, MapperFile output_idx);
    void generateWhileToFile(const WhileStmt& node, MapperFile output_idx);
    void generateDoWhileToFile(const DoWhileStmt& node, MapperFile output_idx);
    void generateMatchToFile(const MatchStmt& node, MapperFile output_idx);
    void emitCompoundScope(const ScopeBlock& n);
    void emitNamespaceScope(const ScopeBlock& n, const std::string& name);
    void visit_ScopeBlock(const ScopeBlock& n);
    void visit_DestructureDecl(const DestructureDecl& n);
    void emitDestructureDict(const DestructureDecl& n);
    void emitDestructureTuple(const DestructureDecl& n);
    void visit_ReturnStmt(const JumpStmt& n);
    void visit_ThrowStmt(const JumpStmt& n);
    void visit_BreakStmt(const JumpStmt& n);
    void visit_ContinueStmt(const JumpStmt& n);
    void visit_GotoStmt(const LabelRef& n);
    void visit_LabelStmt(const LabelRef& n);
    void visit_SemicolonStmt(const SemicolonStmt& n);
    void visit_IfStmt(const IfStmt& n);
    void visit_WhileStmt(const WhileStmt& n);
    void visit_DoWhileStmt(const DoWhileStmt& n);
    void visit_MatchingStmt(const MatchStmt& n);
    void visit_AssignmentStmt(const AstNodeAttr&);
    void visit_BlockStmt(const AstNodeAttr&);
    void visit_ThenBlock(const AstNodeAttr&);
    void visit_ElseBlock(const AstNodeAttr&);
    void visit_WhileElseBlock(const AstNodeAttr&);
    void visit_TryCatchStmt(const Sequence&);
    void visit_CatchBlock(const Sequence&);
    void visit_MatchingCase(const AstNodeAttr&);
    void visit_MatchingElseBlock(const AstNodeAttr&);

  private:
    CppEmitContext& m_ectx;
    CppTranspiler& m_driver;
};

} // namespace trust
