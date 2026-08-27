#pragma once

// include/transpiler/expr_emit.hpp
// Компонент кодогенерации: ExprEmitter. Разделяет CppEmitContext с драйвером CppTranspiler;
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

class ExprEmitter {
  public:
    explicit ExprEmitter(CppEmitContext& ectx, CppTranspiler& driver)
    : m_ectx(ectx)
    , m_driver(driver) {}
    void emitBinaryOpRaw(const Binary& binary_node);
    void emitBinaryOperand(const AstNodeBase* operand, TypeId operandType, TypeId castType);
    void emitBinaryStmtOrExpr(const Binary& binary_node);
    void visit_Attr(const Sequence&);
    void visit_ArgNode(const ArgNode&);
    void visit_AssignOp(const Binary& n);
    void visit_AppendStmt(const Binary& n);
    void visit_MathOp(const Binary& n);
    void visit_BitwiseOp(const Binary& n);
    void visit_CompareOp(const Binary& n);
    void visit_LogicalOp(const Binary& n);
    bool emitDictElementAccess(const Binary& n);
    void visit_MemberAccess(const Binary& n);
    void visit_ArrayAccess(const Binary& n);
    void emitTupleElementAccess(const Binary& n);
    void visit_IntLiteral(const Literal& n);
    void visit_RationalLiteral(const Literal& n);
    void visit_StrChar(const Literal& n);
    void visit_StrWide(const Literal& n);
    void visit_FloatLiteral(const Literal& n);
    void visit_ContextMacro(const ContextMacro&);
    void visit_EmbedExpr(const AstNodeAttr& n);
    void visit_Document(const AstNodeAttr& n);
    void visit_Ident(const IdentName& n);
    void visit_TypeName(const IdentType& n);
    void visit_CallExpr(const CallExpr& n);
    void emitFormatCall(const CallExpr& n);
    void visit_VarRef(const AstNodeAttr&);
    void visit_ArrayInit(const DictLiteralNode& n);
    void emitArrayLiteral(const DictLiteralNode& n, TypeId arrayType);
    void visit_DictLiteral(const DictLiteralNode& n);
    void visit_Tuple(const DictLiteralNode& n);
    void visit_RangeExpr(const RangeExpr& node);
    void emitDictLiteralBody(const DictLiteralNode& n);
    void emitTypedConstruction(const DictLiteralNode& n);
    void emitTypedDictValue(const AstNodeBase* valueNode, TypeId tid);
    void visit_RefMakeExpr(const Sequence&);
    void visit_RefTakeExpr(const Sequence&);
    void visit_Ellipsis(const Sequence&);
    void emitIntrinsic(IntrinsicId id, const CallExpr& call);

  private:
    CppEmitContext& m_ectx;
    CppTranspiler& m_driver;
};

} // namespace trust
