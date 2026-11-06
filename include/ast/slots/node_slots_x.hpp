#pragma once
// ЕДИНСТВЕННЫЙ источник истины «состав узла AST» (ключ — КЛАСС узла).
// Две таблицы AST (ключи не совпадают, поэтому их две):
//   - `PARSER_TOKEN_KINDS` (token.hpp)   — ключ `kind`:  kind -> класс + категория кодогена;
//   - `AST_SLOT_CLASSES` (этот файл)     — ключ `class`: class -> слоты (состав/владение/обход).
// Перечень классов ПРОИЗВОДИТСЯ из `AST_SLOT_CLASSES` (отдельного списка нет).
// Строка: X(shape, field, walk, filter)
//   shape : Ptr        AstNodePtr
//           List       std::vector<AstNodePtr>
//           OptList    std::optional<std::vector<AstNodePtr>>
//           OptRecordList std::optional<std::vector<TemplateArg>> (владеемый узел `rec.node`)
//           DefBody    поле `Definition m_definition`: обходим `field.body` (optional-вектор),
//                      если определение — тело.
//           PairList   std::vector<std::pair<AstNodePtr, AstNodePtr>>
//           CaseList   std::vector<MatchStmt::MatchCase>
//           TargetList std::vector<DestructureDecl::Target> (name/typeNode — владеемые узлы)
//           BaseList   std::vector<TypeBase> (type — владеемый узел; access — данные варианта наследования)
//           ListDeduped std::vector<AstNodePtr>; 4-я колонка = ДРУГОЕ поле этого узла
//                      (элемент, совпадающий с ним по УЗЛУ, не обходится)
//           NonOwnPtr      const AstNodeBase*
//           NonOwnFuncPtr  const FuncDecl*
//           NonOwnList     std::vector<const AstNodeBase*>
//           NonOwnOptList  std::optional<std::vector<const AstNodeBase*>>
//   walk  : Traversed  виден generic-обходу (children()/collectChildren);
//           Internal   владеемый/невладеющий слот, СКРЫТЫЙ от обхода (нужен клону/ремапу).
//   filter: None | SkipKinds(<qualified kinds>) — слот не обходится у перечисленных kinds
//           (политика зависит от KIND, а таблица — по классу, поэтому это ДАННЫЕ строки).
// Порядок строк = порядок обхода (контракт; фиксирован test/unit/ast/slots_test.cpp).
// Слот `AstNodeBase::m_trust` принадлежит ЛЮБОМУ узлу и эмитится базовой реализацией
// `AstNodeBase::forEachSlot` (в таблицу классов не входит).

#define AST_SLOT_CLASSES(C)                       \
    C(Sequence, SLOTS_Sequence)                   \
    C(ScopeBlock, AST_NO_SLOTS)                   \
    C(ModuleNode, AST_NO_SLOTS)                   \
    C(AstNodeAttr, AST_NO_SLOTS)                  \
    C(RawText, AST_NO_SLOTS)                      \
    C(Binary, SLOTS_Binary)                       \
    C(IdentName, SLOTS_IdentName)                 \
    C(IdentType, SLOTS_IdentType)                 \
    C(Literal, SLOTS_Literal)                     \
    C(ArgNode, SLOTS_ArgNode)                     \
    C(ContextMacro, AST_NO_SLOTS)                 \
    C(CheckAreaStmt, AST_NO_SLOTS)                \
    C(DebugStmt, AST_NO_SLOTS)                    \
    C(CallExpr, SLOTS_CallExpr)                   \
    C(JumpStmt, SLOTS_JumpStmt)                   \
    C(DictLiteralNode, SLOTS_DictLiteralNode)     \
    C(RangeExpr, SLOTS_RangeExpr)                 \
    C(RefMakeExpr, AST_NO_SLOTS)                  \
    C(RefTakeExpr, AST_NO_SLOTS)                  \
    C(IfStmt, SLOTS_IfStmt)                       \
    C(WhileStmt, SLOTS_WhileStmt)                 \
    C(DoWhileStmt, SLOTS_DoWhileStmt)             \
    C(MatchStmt, SLOTS_MatchStmt)                 \
    C(WithStmt, SLOTS_WithStmt)                   \
    C(TryCatchStmt, SLOTS_TryCatchStmt)           \
    C(CatchBlock, SLOTS_CatchBlock)               \
    C(LabelRef, AST_NO_SLOTS)                     \
    C(SemicolonStmt, SLOTS_SemicolonStmt)         \
    C(LastResultCapture, SLOTS_LastResultCapture) \
    C(ErrorExpr, SLOTS_ErrorExpr)                 \
    C(TrustContract, SLOTS_TrustContract)         \
    C(TrustElem, SLOTS_TrustElem)                 \
    C(FuncDecl, SLOTS_FuncDecl)                   \
    C(VarDecl, SLOTS_VarDecl)                     \
    C(ClassDecl, SLOTS_ClassDecl)                 \
    C(RecordDecl, SLOTS_RecordDecl)               \
    C(DestructureDecl, SLOTS_DestructureDecl)

#define AST_SLOT_BASE_CLASSES(C) \
    C(Decl, SLOTS_Decl)          \
    C(ControlFlowStmt, SLOTS_ControlFlowStmt)

// Общая строка для классов без собственных слотов.
#define AST_NO_SLOTS(X)

#define SLOTS_Sequence(X) X(List, m_body, Traversed, SkipKinds(ParserToken::Kind::TypeSet))

#define SLOTS_Binary(X)                                                                            \
    X(Ptr, m_left, Traversed, SkipKinds(ParserToken::Kind::TypeDecl, ParserToken::Kind::NameDecl)) \
    X(Ptr, m_right, Traversed, None)                                                               \
    X(ListDeduped, m_indices, Traversed, m_right)                                                  \
    X(NonOwnPtr, m_typeDecl, Internal, None)

#define SLOTS_IdentName(X) X(NonOwnPtr, resolvedDecl, Internal, None)

#define SLOTS_IdentType(X)                \
    X(OptList, m_dims, Traversed, None)   \
    X(OptList, m_params, Traversed, None) \
    X(OptRecordList, m_templateArgs, Internal, None)

#define SLOTS_Literal(X) X(Ptr, typeAnnotation, Internal, None)

#define SLOTS_ArgNode(X)             \
    X(Ptr, m_type, Traversed, None)  \
    X(Ptr, m_value, Traversed, None) \
    X(NonOwnPtr, resolvedDecl, Internal, None)

#define SLOTS_CallExpr(X)               \
    X(Ptr, m_callee, Traversed, None)   \
    X(OptList, m_args, Traversed, None) \
    X(NonOwnOptList, boundArgs, Internal, None)

#define SLOTS_JumpStmt(X)               \
    X(Ptr, m_tempDecl, Traversed, None) \
    X(Ptr, m_value, Traversed, None)    \
    X(Ptr, m_label, Internal, None)     \
    X(NonOwnFuncPtr, m_funcDecl, Internal, None)

#define SLOTS_DictLiteralNode(X)   \
    X(Ptr, m_type, Internal, None) \
    X(Ptr, arrayElementAnnotation, Internal, None)

#define SLOTS_RangeExpr(X) X(List, operandTypes, Internal, None)

#define SLOTS_IfStmt(X) X(PairList, m_elseifs, Traversed, None)

#define SLOTS_WhileStmt(X)              \
    X(Ptr, m_elseFlag, Traversed, None) \
    X(NonOwnList, m_invariants, Internal, None)

#define SLOTS_DoWhileStmt(X) X(NonOwnList, m_invariants, Internal, None)

#define SLOTS_MatchStmt(X)                    \
    X(Ptr, m_tempDecl, Traversed, None)       \
    X(Ptr, m_value, Traversed, None)          \
    X(CaseList, m_cases, Traversed, None)     \
    X(Ptr, m_default, Traversed, None)        \
    X(NonOwnPtr, m_matcherDecl, Internal, None)

#define SLOTS_WithStmt(X)                 \
    X(PairList, m_locks, Traversed, None) \
    X(Ptr, m_body, Traversed, None)       \
    X(Ptr, m_else, Traversed, None)       \
    X(Ptr, m_failFlag, Internal, None)

#define SLOTS_TryCatchStmt(X)           \
    X(List, m_catches, Traversed, None) \
    X(Ptr, m_else, Traversed, None)

#define SLOTS_CatchBlock(X) X(Ptr, m_binding, Internal, None)

#define SLOTS_SemicolonStmt(X) X(Ptr, m_expr, Traversed, None)

#define SLOTS_LastResultCapture(X)    \
    X(Ptr, m_temp, Traversed, None)   \
    X(Ptr, m_source, Traversed, None) \
    X(Ptr, m_sink, Traversed, None)

#define SLOTS_ErrorExpr(X) X(Ptr, m_original, Internal, None)

#define SLOTS_TrustContract(X) X(Ptr, m_expr, Internal, None)

#define SLOTS_TrustElem(X) X(List, m_args, Internal, None)

#define SLOTS_FuncDecl(X)                        \
    X(Ptr, m_aliasTarget, Traversed, None)       \
    X(OptList, m_params, Traversed, None)        \
    X(DefBody, m_definition, Traversed, None)          \
    X(OptList, m_templateParams, Internal, None) \
    X(OptList, m_captures, Internal, None)

#define SLOTS_VarDecl(X)                   \
    X(Ptr, m_initializer, Traversed, None) \
    X(NonOwnPtr, m_typeDecl, Internal, None)

#define SLOTS_ClassDecl(X)                       \
    X(OptList, m_templateParams, Internal, None) \
    X(List, m_body, Traversed, None)

#define SLOTS_RecordDecl(X)                      \
    X(BaseList, m_baseTypes, Internal, None)     \
    X(OptList, m_templateParams, Internal, None) \
    X(DefBody, m_definition, Traversed, None)

#define SLOTS_DestructureDecl(X)                          \
    X(Ptr, m_source, Traversed, None)                     \
    X(TargetList, m_targets, Internal, None)              \
    X(Ptr, m_sourceTemp, Internal, None)

#define SLOTS_Decl(X) X(Ptr, m_type, Traversed, None)

#define SLOTS_ControlFlowStmt(X)    \
    X(Ptr, m_cond, Traversed, None) \
    X(Ptr, m_body, Traversed, None) \
    X(Ptr, m_else, Traversed, None)