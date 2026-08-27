#pragma once

#include "ast/ast_nodes.hpp"
#include "ast/kind_visitor.hpp"
#include "location/location.hpp"
#include "transpiler/emit_ctx.hpp"
#include "transpiler/type_emit.hpp"
#include "transpiler/decl_emit.hpp"
#include "transpiler/stmt_emit.hpp"
#include "transpiler/expr_emit.hpp"
#include "transpiler/contract_emit.hpp"
#include "types/runtime_symbols.hpp"
#include "types/type_id.hpp"
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class Context;
class SymbolTable;

/// CppTranspiler - генератор C++ кода из AST, НАСЛЕДНИК KindVisitor (фасад).
/// Вся тяжёлая кодогенерация вынесена в компоненты-эмиттеры (TypeEmitter/DeclEmitter/
/// StmtEmitter/ExprEmitter/ContractEmitter), разделяющие общий CppEmitContext (m_ectx).
/// Здесь - публичный API, обход/диспетчеризация (visit_<Kind> делегируют эмиттерам) и
/// рекурсивные точки emitExpr/emitSequenceBody. Эмиттеры - друзья: им нужен доступ к
/// приватной рекурсии (emitExpr) и друг к другу (через ссылки на эмиттеры m_type/...).
class CppTranspiler : public KindVisitor {
  public:
    explicit CppTranspiler(Context& ctx, const SymbolTable* resolvedTypes = nullptr);

    /// Генерация C++ кода непосредственно в выходной файл с построением source map.
    /// Для каждого узла AST создаётся маппинг trust-range → cpp-range через mapStart/mapStop.
    void generateToFile(const std::vector<AstNodePtr>& ast_nodes, MapperFile output_idx);

    /// Список всех экспортированных символов (собран в процессе generateToFile).
    const std::vector<ExportEntry>& exports() const noexcept { return m_ectx.m_exports; }

    /// Пути рантайм-заголовков (например "trust/rational.hpp"), которые реально
    /// понадобились сгенерированному коду (маркер '@' в preprocInclude типа).
    const std::set<std::string>& runtimeHeaders() const noexcept { return m_ectx.m_runtimeHeaders; }

    /// Флаги линковки нативных библиотек (`-l<имя>`) из атрибутов `@[link("имя")]`.
    const std::set<std::string>& linkLibs() const noexcept { return m_ectx.m_linkLibs; }

  private:
    /// Единая диспетчеризация по kind: dispatchKind(node, *this).
    void generateNodeToFile(const AstNodeBase& node, MapperFile output_idx);

    /// Вывод выражения в поток source() на текущую позицию (без statement-терминатора).
    void emitExpr(const AstNodeBase* node);

    /// Генерация тела блока (Sequence/ScopeBlock/ModuleNode): обход m_body.
    void emitSequenceBody(const Sequence& node, MapperFile output_idx);

    /// Выводит "{}" (placeholder) только в expression-контексте (m_exprDepth>0).
    void emitPlaceholderExpr(MapperFile output_idx);

    /// Унифицированный вывод текста как вложенного выражения (только при m_exprDepth>0).
    void emitExprText(std::string_view text);

    /// Унифицированная генерация return/throw: `keyword` = "return"/"throw".
    void emitJumpValue(std::string_view keyword, const JumpStmt& node);

    /// Выводит перевод строки между последовательными блоками, если строки в исходнике различаются.
    void emitBlockSeparator(const AstNodeBase* prev, const AstNodeBase& node, MapperFile output_idx);

    /// Для блоков на одной строке исходника: вставляет пробел между ними.
    void emitSameLineSpace(std::string_view nextText, MapperFile output_idx);

    /// Эмиссия тела блока `{ ... }`: body - тело (ScopeBlock/Sequence → m_body, иначе одиночный
    /// statement). Дефолты позволяют управляющим конструкциям вызывать с 2/3/5 аргументами.
    void emitBodyNode(const AstNodePtr& body, MapperFile output_idx, bool mapBlock = false, const std::string& beforeCloseLabel = "",
                      const std::string& afterOpen = "");

    /// True, если документирующие комментарии подавлены (флаг -Wno-comments).
    [[nodiscard]] bool isSuppressedDoc(ParserToken::Kind k) const;

    /// Эмитит документирующий комментарий, привязанный к узлу объявления.
    void emitDocumentation(const AstNodeBase& node, MapperFile output_idx);

    // -- KindVisitor: visit_<Kind> - делегируют эмиттерам --
    void visit_sequence(const Sequence& node) override;
    void visit_Attr(const Sequence& node) override;
    void visit_ScopeBlock(const ScopeBlock& node) override;
    void visit_TypeDecl(const Binary& node) override;
    void visit_NameDecl(const Binary& node) override;
    void visit_AssignOp(const Binary& node) override;
    void visit_AppendStmt(const Binary& node) override;
    void visit_MathOp(const Binary& node) override;
    void visit_BitwiseOp(const Binary& node) override;
    void visit_CompareOp(const Binary& node) override;
    void visit_LogicalOp(const Binary& node) override;
    void visit_MemberAccess(const Binary& node) override;
    void visit_ArrayAccess(const Binary& node) override;
    void visit_Ident(const IdentName& node) override;
    void visit_TypeName(const IdentType& node) override;
    void visit_CallExpr(const CallExpr& node) override;
    void visit_ReturnStmt(const JumpStmt& node) override;
    void visit_ThrowStmt(const JumpStmt& node) override;
    void visit_Program(const AstNodeAttr& node) override;
    void visit_VarRef(const AstNodeAttr& node) override;
    void visit_EmbedExpr(const AstNodeAttr& node) override;
    void visit_Document(const AstNodeAttr& node) override;
    void visit_IntLiteral(const Literal& node) override;
    void visit_FloatLiteral(const Literal& node) override;
    void visit_StrChar(const Literal& node) override;
    void visit_StrWide(const Literal& node) override;
    void visit_RationalLiteral(const Literal& node) override;
    void visit_ArrayInit(const DictLiteralNode& node) override;
    void visit_DictLiteral(const DictLiteralNode& node) override;
    void visit_Tuple(const DictLiteralNode& node) override;
    void visit_RangeExpr(const RangeExpr& node) override;
    void visit_RefMakeExpr(const Sequence& node) override;
    void visit_RefTakeExpr(const Sequence& node) override;
    void visit_Ellipsis(const Sequence& node) override;
    void visit_IfStmt(const IfStmt& node) override;
    void visit_WhileStmt(const WhileStmt& node) override;
    void visit_AssignmentStmt(const AstNodeAttr& node) override;
    void visit_SemicolonStmt(const SemicolonStmt& node) override;
    void visit_BlockStmt(const AstNodeAttr& node) override;
    void visit_ThenBlock(const AstNodeAttr& node) override;
    void visit_ElseBlock(const AstNodeAttr& node) override;
    void visit_DoWhileStmt(const DoWhileStmt& node) override;
    void visit_WhileElseBlock(const AstNodeAttr& node) override;
    void visit_BreakStmt(const JumpStmt& node) override;
    void visit_ContinueStmt(const JumpStmt& node) override;
    void visit_GotoStmt(const LabelRef& node) override;
    void visit_LabelStmt(const LabelRef& node) override;
    void visit_TryCatchStmt(const Sequence& node) override;
    void visit_CatchBlock(const Sequence& node) override;
    void visit_MatchingStmt(const MatchStmt& node) override;
    void visit_MatchingCase(const AstNodeAttr& node) override;
    void visit_MatchingElseBlock(const AstNodeAttr& node) override;
    void visit_FuncDecl(const FuncDecl& node) override;
    void visit_VarDecl(const VarDecl& node) override;
    void visit_DestructureDecl(const DestructureDecl& node) override;
    void visit_ArgNode(const ArgNode& node) override;
    void visit_EnumDecl(const Sequence& node) override;
    void visit_EnumMember(const Sequence& node) override;
    void visit_StructDecl(const Sequence& node) override;
    void visit_StructField(const Sequence& node) override;
    void visit_ModuleDecl(const ModuleNode& node) override;
    void visit_Unimplemented(const AstNodeAttr& node) override;
    void visit_NotApplicable(const AstNodeAttr& node) override;
    void visit_ContextMacro(const ContextMacro& node) override;
    void visit_TrustContract(const TrustContract& node) override;
    void visit_TrustElem(const TrustElem& node) override;

    /// Общий контекст кодогенерации (состояние + низкоуровневые операции вывода/маппинга).
    CppEmitContext m_ectx;

    /// Компоненты-эмиттеры (владеют тяжёлой логикой кодогенерации).
    TypeEmitter m_type;
    DeclEmitter m_decl;
    StmtEmitter m_stmt;
    ExprEmitter m_expr;
    ContractEmitter m_contract;

    friend class TypeEmitter;
    friend class DeclEmitter;
    friend class StmtEmitter;
    friend class ExprEmitter;
    friend class ContractEmitter;
};
} // namespace trust
