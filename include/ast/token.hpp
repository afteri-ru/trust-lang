// token.hpp - public header for the token library.
// Provides ParserToken::Kind enum for AST nodes only.
// Kind is generated from PARSER_TOKEN_KINDS X-macro.
// To add a new token kind, add an entry to PARSER_TOKEN_KINDS.
// Do NOT modify the enum, name() manually.
//
// Format: T(name, node_type)
//   name     - enumerator name (CamelCase)
//   node_type - C++ class that represents this Kind (e.g. Binary, Scope, IdentName)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include "location/location.hpp"
#include "utils/error.hpp"

namespace trust {

class AstNodeBase;

// Forward declarations for concrete AST node types (full definitions in their respective headers)
class AstNodeAttr;
class Binary;
class CallExpr;
class Sequence;
class ScopeBlock;
class DictLiteralNode;
class RangeExpr;
class RefMakeExpr;
class RefTakeExpr;
class IdentName;
class IdentType;
class Decl;
class Literal;
class JumpStmt;
class ModuleNode;
class ArgNode;
class FuncDecl;
class VarDecl;
class ClassDecl;
class RecordDecl;
class DestructureDecl;
class IfStmt;
class WhileStmt;
class DoWhileStmt;
class MatchStmt;
class WithStmt;
class TryCatchStmt;
class CatchBlock;
class LabelRef;
class SemicolonStmt;
class LastResultCapture;
class ErrorExpr;
class ContextMacro;
class TrustContract;
class TrustElem;
class CheckAreaStmt;
class DebugStmt;

// ============================================================================
// X-macro: all ParserToken kinds.
//
// Format: T(name, node_type)
//   name      - enumerator name (CamelCase)
//   node_type - C++ class that stores data for this token kind.
//               Multiple kinds can share the same node_type.
//
// Grouped by node_type for readability.
// ============================================================================
#define PARSER_TOKEN_KINDS(T)                                         \
    /* -- Sequence -- */                                              \
    T(sequence, Sequence)                                             \
    T(Attr, Sequence)                                                 \
    /* -- ScopeBlock -- */                                            \
    T(ScopeBlock, ScopeBlock)                                         \
    /* -- Binary -- */                                                \
    T(TypeDecl, Binary)                                               \
    T(NameDecl, Binary)                                               \
    T(AssignOp, Binary)                                               \
    T(AppendStmt, Binary)                                             \
    T(MathOp, Binary)                                                 \
    T(BitwiseOp, Binary)                                              \
    T(CompareOp, Binary)                                              \
    T(LogicalOp, Binary)                                              \
    T(MemberAccess, Binary)                                           \
    T(ArrayAccess, Binary)                                            \
    /* Набор допустимых типов: члены в m_sequence (Sequence::m_body). */ \
    T(TypeSet, Sequence)                                              \
    /* -- IdentName -- */                                             \
    T(Ident, IdentName)                                               \
    /* -- IdentType -- */                                             \
    T(TypeName, IdentType)                                            \
    /* -- CallExpr -- */                                              \
    T(CallExpr, CallExpr)                                             \
    /* -- JumpStmt -- */                                              \
    T(ReturnStmt, JumpStmt)                                           \
    T(ThrowStmt, JumpStmt)                                            \
    /* -- AstNodeAttr -- */                                           \
    T(Program, AstNodeAttr)                                           \
    T(VarRef, AstNodeAttr)                                            \
    T(EmbedExpr, AstNodeAttr)                                         \
    T(Document, AstNodeAttr)                                          \
    T(ContextMacro, ContextMacro)                                     \
    T(CheckAreaStmt, CheckAreaStmt)                                   \
    T(DebugStmt, DebugStmt)                                           \
    T(Unimplemented, AstNodeAttr)                                     \
    T(NotApplicable, AstNodeAttr)                                     \
    T(IntLiteral, Literal)                                            \
    T(FloatLiteral, Literal)                                          \
    T(StrChar, Literal)                                               \
    T(StrWide, Literal)                                               \
    T(RationalLiteral, Literal)                                       \
    T(ArrayInit, DictLiteralNode)                                     \
    T(DictLiteral, DictLiteralNode)                                   \
    T(Tuple, DictLiteralNode)                                         \
    T(RangeExpr, RangeExpr)                                           \
    T(RefMakeExpr, RefMakeExpr)                                       \
    T(RefTakeExpr, RefTakeExpr)                                       \
    T(RefLockExpr, Sequence)                                          \
    T(RefLockDeref, Sequence)                                         \
    T(Ellipsis, Sequence)                                             \
    T(Filling, Sequence)                                              \
    T(IfStmt, IfStmt)                                                 \
    T(WhileStmt, WhileStmt)                                           \
    T(AssignmentStmt, AstNodeAttr)                                    \
    T(SemicolonStmt, SemicolonStmt)                                   \
    T(LastResultCapture, LastResultCapture)                           \
    T(ErrorExpr, ErrorExpr)                                           \
    T(BlockStmt, AstNodeAttr)                                         \
    T(ThenBlock, AstNodeAttr)                                         \
    T(ElseBlock, AstNodeAttr)                                         \
    T(DoWhileStmt, DoWhileStmt)                                       \
    T(WhileElseBlock, AstNodeAttr)                                    \
    T(BreakStmt, JumpStmt)                                            \
    T(ContinueStmt, JumpStmt)                                         \
    T(GotoStmt, LabelRef)                                             \
    T(LabelStmt, LabelRef)                                            \
    T(TryCatchStmt, TryCatchStmt)                                     \
    T(CatchBlock, CatchBlock)                                         \
    T(MatchingStmt, MatchStmt)                                        \
    T(WithStmt, WithStmt)                                             \
    T(MatchingCase, AstNodeAttr)                                      \
    T(MatchingElseBlock, AstNodeAttr)                                 \
    T(FuncDecl, FuncDecl)                                             \
    T(VarDecl, VarDecl)                                               \
    T(ClassDecl, ClassDecl)                                           \
    T(DestructureDecl, DestructureDecl)                               \
    /* -- ArgNode -- */                                               \
    T(ArgNode, ArgNode)                                               \
    T(EnumDecl, Sequence)                                             \
    T(EnumMember, Sequence)                                           \
    T(StructDecl, RecordDecl)                                         \
    T(StructField, Sequence)                                          \
    /* -- TrustContract (единая trust-конструкция, kind в поле) -- */ \
    T(TrustContract, TrustContract)                                   \
    /* -- TrustElem (элемент контрактного программирования) -- */     \
    T(TrustElem, TrustElem)                                           \
    /* -- ModuleNode -- */                                            \
    T(ModuleDecl, ModuleNode)

/** Unified enum for all AST node types (CamelCase).
 *  Generated from PARSER_TOKEN_KINDS - do not edit manually. */
struct ParserToken {
    enum class Kind : int {
        END = 0,
#define TOK_ENUM(name, node_type) name,
        PARSER_TOKEN_KINDS(TOK_ENUM)
#undef TOK_ENUM
    };

    // Token name
    [[nodiscard]] static constexpr std::string_view name(Kind k) noexcept {
        switch (k) {
        case Kind::END:
            return "END";
#define TOK_NAME(name, node_type) \
    case Kind::name:              \
        return #name;
            PARSER_TOKEN_KINDS(TOK_NAME)
#undef TOK_NAME
        }
        return "<unknown>";
    }
};

// -- Kind → C++ type mapping --
// Each ParserToken::Kind maps to the concrete C++ class that stores its data.

template <ParserToken::Kind K>
struct NodeTypeForKind;

template <ParserToken::Kind K>
using node_type_for_kind_t = typename NodeTypeForKind<K>::type;

#define TOK_NODE_TYPE(name, node_type)                \
    template <>                                       \
    struct NodeTypeForKind<ParserToken::Kind::name> { \
        using type = node_type;                       \
    };
PARSER_TOKEN_KINDS(TOK_NODE_TYPE)
#undef TOK_NODE_TYPE

// PARSER_TOKEN_KINDS намеренно НЕ #undef'ится: он переиспользуется в ast/kind_visitor.hpp
// для генерации kind-визитора (интерфейс + диспетчер). Там же в конце файла он #undef'ится.

// END maps to AstNodeBase (same as any other node without special fields).
template <>
struct NodeTypeForKind<ParserToken::Kind::END> {
    using type = AstNodeBase;
};

// ============================================================================
// Канонический реестр классов AST-узлов (R8).
//
// Архитектурный инвариант (trap из include/ast/MEMORY.md): класс AST - это ФОРМАТ ХРАНЕНИЯ
// данных, а не идентичность узла. РАЗНЫЕ kinds могут отображаться на ОДИН класс (Binary,
// Sequence, ...); отношение kind->класс задаётся ВТОРЫМ полем PARSER_TOKEN_KINDS.
// ЗАПРЕЩЕНО заводить новый класс под новый kind - нужно переиспользовать существующий.
//
// AST_NODE_CLASS_LIST - канонический перечень всех классов, которые допускается указывать
// вторым полем PARSER_TOKEN_KINDS. static_assert ниже делает инвариант машинно-проверяемым:
//   - каждый kind маппится на класс ИЗ списка (иначе - ошибка компиляции);
//   - каждый класс списка используется хотя бы одним kind (нет «мёртвых» классов).
// Расширение списка допустимо только вместе с настоящей новой сущностью AST-узла (структурой
// данных), а не «на новый kind».
// ============================================================================
#define AST_NODE_CLASS_LIST(M)                                                                                                                                 \
    M(Sequence)                                                                                                                                                \
    M(ScopeBlock) M(ModuleNode) M(Binary) M(IdentName) M(IdentType) M(Literal) M(ArgNode) M(ContextMacro) M(CheckAreaStmt) M(DebugStmt) M(CallExpr)            \
        M(JumpStmt) M(AstNodeAttr) M(DictLiteralNode) M(RangeExpr) M(RefMakeExpr) M(RefTakeExpr) M(IfStmt) M(WhileStmt) M(DoWhileStmt) M(MatchStmt)            \
            M(WithStmt) M(TryCatchStmt) M(CatchBlock) M(LabelRef) M(SemicolonStmt) M(LastResultCapture) M(ErrorExpr) M(TrustContract) M(TrustElem) M(FuncDecl) \
                M(VarDecl) M(ClassDecl) M(RecordDecl) M(DestructureDecl)

/// Traits: класс AST-узла из канонического списка? Даёт человекочитаемое имя для диагностик.
template <class T>
struct AstNodeClassInfo {
    static constexpr bool kKnown = false;
    static constexpr std::string_view kName = "<not-an-AST-node-class>";
};

#define AST_NODE_CLASS_ENTRY(cls)                       \
    template <>                                         \
    struct AstNodeClassInfo<cls> {                      \
        static constexpr bool kKnown = true;            \
        static constexpr std::string_view kName = #cls; \
    };
AST_NODE_CLASS_LIST(AST_NODE_CLASS_ENTRY)
#undef AST_NODE_CLASS_ENTRY

#define AST_NODE_CLASS_ASSERT(name, node_type)                                                                                                   \
    static_assert(AstNodeClassInfo<node_type>::kKnown, "PARSER_TOKEN_KINDS: kind '" #name "' maps to class '" #node_type                         \
                                                       "' which is not in AST_NODE_CLASS_LIST; reuse an existing AST node class for a new kind " \
                                                       "(do not introduce a new class per kind)");
PARSER_TOKEN_KINDS(AST_NODE_CLASS_ASSERT)
#undef AST_NODE_CLASS_ASSERT

namespace detail {

/// Индекс класса в каноническом списке (для проверки «нет мёртвых классов»).
#define AST_CLASS_ID_ENTRY(cls) k##cls,
enum class AstClassId : std::size_t { AST_NODE_CLASS_LIST(AST_CLASS_ID_ENTRY) kCount };
#undef AST_CLASS_ID_ENTRY

template <class T>
struct AstClassIdOf;

#define AST_CLASS_ID_SPEC(cls)                                  \
    template <>                                                 \
    struct AstClassIdOf<cls> {                                  \
        static constexpr AstClassId value = AstClassId::k##cls; \
    };
AST_NODE_CLASS_LIST(AST_CLASS_ID_SPEC)
#undef AST_CLASS_ID_SPEC

constexpr std::size_t kAstClassCount = static_cast<std::size_t>(AstClassId::kCount);

/// Массив «класс используется хотя бы одним kind» - заполняется из PARSER_TOKEN_KINDS.
constexpr std::array<bool, kAstClassCount> astClassUsed() noexcept {
    std::array<bool, kAstClassCount> used{};
#define AST_CLASS_MARK(name, node_type) used[static_cast<std::size_t>(AstClassIdOf<node_type>::value)] = true;
    PARSER_TOKEN_KINDS(AST_CLASS_MARK)
#undef AST_CLASS_MARK
    return used;
}

constexpr std::array<bool, kAstClassCount> kAstClassUsed = astClassUsed();

template <std::size_t... I>
constexpr bool allAstClassesUsed(std::index_sequence<I...>) noexcept {
    return (kAstClassUsed[I] && ...);
}

static_assert(allAstClassesUsed(std::make_index_sequence<kAstClassCount>{}),
              "AST_NODE_CLASS_LIST contains a class that no PARSER_TOKEN_KINDS kind maps to (dead AST node class)");

} // namespace detail

using AstNodePtr = std::shared_ptr<AstNodeBase>;

} // namespace trust