// include/ast/ast_nodes.hpp
// Специализированные AST-узлы: Literal, Sequence, ScopeBlock, ModuleNode, Binary,
// CallExpr, JumpStmt, ControlFlowStmt/IfStmt/WhileStmt/DoWhileStmt, MatchStmt,
// ArgNode, Decl/VarDecl/FuncDecl, LabelRef, SemicolonStmt, ContextMacro.
//
// Иерархия (полная - см. ast/MEMORY.md «Полная иерархия»; здесь - главные ветви):
//   AstNodeBase (token_base.hpp) - kind + MapperRange (provenance); Term НЕ хранится.
//     └-- AstNodeAttr - +attrs; текст у базовых узлов ОТСУТСТВУЕТ.
//           ├-- WithText<AstNodeAttr> - mixin-носитель текста (HasText::text):
//           │     ├-- Literal      (Int|Float|StrChar|StrWide)
//           │     ├-- ContextMacro (лист-маркер, раскрывается в семантике)
//           │     ├-- ArgNode      (+m_type, m_value; ЕДИНЫЙ узел аргумента)
//           │     ├-- LabelRef     (GotoStmt|LabelStmt: имя метки) - СИНТЕТИЧЕСКИЙ (lowering)
//           │     ├-- IdentName    (Ident; идентификатор + методы)  - ident_name.hpp
//           │     │     ├-- IdentType   (TypeName, +m_dims, m_params)  - token_type.hpp
//           │     │     └-- Decl        (+m_type)
//           │     │           ├-- VarDecl     (+m_initializer)
//           │     │           └-- FuncDecl    (+m_params, m_body)
//           │     └-- RawText      (Document|EmbedExpr: текст как есть, не интерпретируется)
//           ├-- Sequence   (верхний уровень файла, БЕЗ имени/текста; +m_body)
//           │     ├-- WithText<Sequence> - именованные области:
//           │     │     ├-- ScopeBlock  (Block: имя namespace/метки, +m_blockCounter)
//           │     │     └-- ModuleNode  (module id, +m_moduleIndices/parts)
//           │     ├-- TryCatchStmt (+TryKind, m_catches, m_else)
//           │     ├-- CatchBlock, DictLiteralNode, RangeExpr, RefTakeExpr, RefMakeExpr
//           │     └-- kinds sequence|Attr|TypeSet|Enum*|StructField|Ellipsis|Filling|RefLock*
//           ├-- Binary      (TypeDecl|NameDecl|AssignOp|MathOp|BitwiseOp|CompareOp|
//           │                LogicalOp|MemberAccess|ArrayAccess, +m_left, m_right; глиф - m_op)
//           ├-- CallExpr    (+m_callee, m_args)
//           ├-- JumpStmt    (ReturnStmt|ThrowStmt|BreakStmt|ContinueStmt, +m_label, m_value)
//           ├-- SemicolonStmt (+m_expr) - СИНТЕТИЧЕСКИЙ (lowering)
//           ├-- ControlFlowStmt (+m_cond, m_body, m_else)
//           │     ├-- IfStmt       (+m_elseifs)
//           │     ├-- WhileStmt
//           │     └-- DoWhileStmt
//           └-- AstNodeAttr (leaf-kind'ы: Program|VarRef|...)
//
// Поля узлов - AstNodePtr; списки узлов - std::vector<AstNodePtr>.
//
// Конструкторы с TermPtr - основной путь создания (из терминов синтаксического дерева).
// Конструкторы без Term (test-only) существуют для unit-тестов; text() есть ТОЛЬКО у классов,
// наследующих WithText (HasText): у прочих узлов метода text() нет (compile-time), а обобщённый
// код читает текст через as_text()/requiredText(); range() синтетического узла невалиден.

#pragma once

#include "ast/token_base.hpp"
#include "ast/binary_op.hpp"
#include "ast/token.hpp"
#include "syntax/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_type.hpp"
#include "diag/severity.hpp"
#include "ast/trust_prop.hpp"
#include "ast/nodes/z3_term.hpp"
#include "types/type_id.hpp"
#include "utils/error.hpp"
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "ast/nodes/expr_nodes.hpp"
#include "ast/nodes/decl_nodes.hpp"
#include "ast/nodes/stmt_nodes.hpp"
#include "ast/nodes/contract_nodes.hpp"

namespace trust {

// ============================================================================
// AstNodeBase::is<T>() / as<T>() - типизированный доступ к узлу БЕЗ RTTI (R1).
// Определены здесь: для std::is_base_of_v нужны ПОЛНЫЕ определения всех классов AST;
// отображение kind->класс - из PARSER_TOKEN_KINDS (token.hpp).
// ============================================================================

template <class T>
bool AstNodeBase::is() const noexcept {
    switch (m_kind) {
#define AST_IS_CASE(name, node_type, category) \
    case ParserToken::Kind::name:              \
        return std::is_base_of_v<T, node_type>;
        PARSER_TOKEN_KINDS(AST_IS_CASE)
#undef AST_IS_CASE
    case ParserToken::Kind::END:
        return false;
    }
}

template <class T>
T* AstNodeBase::as() {
    static_assert(AstNodeClassInfo<T>::kKnown, "as<T>(): T must be an AST node class from AST_SLOT_CLASSES");
    if (!is<T>()) {
        FAULT("AstNodeBase::as<{}>(): node kind '{}' is not an instance of {}", AstNodeClassInfo<T>::kName, ParserToken::name(m_kind),
              AstNodeClassInfo<T>::kName);
    }
    return static_cast<T*>(this);
}

template <class T>
const T* AstNodeBase::as() const {
    static_assert(AstNodeClassInfo<T>::kKnown, "as<T>(): T must be an AST node class from AST_SLOT_CLASSES");
    if (!is<T>()) {
        FAULT("AstNodeBase::as<{}>(): node kind '{}' is not an instance of {}", AstNodeClassInfo<T>::kName, ParserToken::name(m_kind),
              AstNodeClassInfo<T>::kName);
    }
    return static_cast<const T*>(this);
}

} // namespace trust