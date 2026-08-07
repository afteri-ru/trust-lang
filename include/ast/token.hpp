// token.hpp - public header for the token library.
// Provides ParserToken::Kind enum for AST nodes only.
// Kind is generated from PARSER_TOKEN_KINDS X-macro.
// To add a new token kind, add an entry to PARSER_TOKEN_KINDS.
// Do NOT modify the enum, name() manually.
//
// Format: T(name, node_type)
//   name     — enumerator name (CamelCase)
//   node_type — C++ class that represents this Kind (e.g. Binary, Scope, IdentName)

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
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
class IdentName;
class IdentType;
class Decl;
class Literal;
class JumpStmt;
class ModuleNode;
class ArgNode;
class FuncDecl;
class VarDecl;
class DestructureDecl;
class IfStmt;
class WhileStmt;
class DoWhileStmt;
class MatchStmt;
class LabelRef;
class SemicolonStmt;
class ContextMacro;

// ============================================================================
// X-macro: all ParserToken kinds.
//
// Format: T(name, node_type)
//   name      — enumerator name (CamelCase)
//   node_type — C++ class that stores data for this token kind.
//               Multiple kinds can share the same node_type.
//
// Grouped by node_type for readability.
// ============================================================================
#define PARSER_TOKEN_KINDS(T)           \
    /* ── Sequence ── */                \
    T(sequence, Sequence)               \
    T(Attr, Sequence)                   \
    /* ── ScopeBlock ── */              \
    T(ScopeBlock, ScopeBlock)           \
    /* ── Binary ── */                  \
    T(TypeDecl, Binary)                 \
    T(NameDecl, Binary)                 \
    T(AssignOp, Binary)                 \
    T(AppendStmt, Binary)               \
    T(MathOp, Binary)                   \
    T(BitwiseOp, Binary)                \
    T(CompareOp, Binary)                \
    T(LogicalOp, Binary)                \
    T(MemberAccess, Binary)             \
    T(ArrayAccess, Binary)              \
    /* ── IdentName ── */               \
    T(Ident, IdentName)                 \
    /* ── IdentType ── */               \
    T(TypeName, IdentType)              \
    /* ── CallExpr ── */                \
    T(CallExpr, CallExpr)               \
    /* ── JumpStmt ── */                \
    T(ReturnStmt, JumpStmt)             \
    T(ThrowStmt, JumpStmt)              \
    /* ── AstNodeAttr ── */             \
    T(Program, AstNodeAttr)             \
    T(VarRef, AstNodeAttr)              \
    T(EmbedExpr, AstNodeAttr)           \
    T(Document, AstNodeAttr)            \
    T(ContextMacro, ContextMacro)       \
    T(Unimplemented, AstNodeAttr)       \
    T(NotApplicable, AstNodeAttr)       \
    T(IntLiteral, Literal)              \
    T(FloatLiteral, Literal)            \
    T(StrChar, Literal)                 \
    T(StrWide, Literal)                 \
    T(RationalLiteral, Literal)         \
    T(ArrayInit, DictLiteralNode)       \
    T(DictLiteral, DictLiteralNode)     \
    T(Tuple, DictLiteralNode)           \
    T(RangeExpr, RangeExpr)             \
    T(RefMakeExpr, Sequence)            \
    T(RefTakeExpr, Sequence)            \
    T(Ellipsis, Sequence)               \
    T(IfStmt, IfStmt)                   \
    T(WhileStmt, WhileStmt)             \
    T(AssignmentStmt, AstNodeAttr)      \
    T(SemicolonStmt, SemicolonStmt)     \
    T(BlockStmt, AstNodeAttr)           \
    T(ThenBlock, AstNodeAttr)           \
    T(ElseBlock, AstNodeAttr)           \
    T(DoWhileStmt, DoWhileStmt)         \
    T(WhileElseBlock, AstNodeAttr)      \
    T(BreakStmt, JumpStmt)              \
    T(ContinueStmt, JumpStmt)           \
    T(GotoStmt, LabelRef)               \
    T(LabelStmt, LabelRef)              \
    T(TryCatchStmt, Sequence)           \
    T(CatchBlock, Sequence)             \
    T(MatchingStmt, MatchStmt)          \
    T(MatchingCase, AstNodeAttr)        \
    T(MatchingElseBlock, AstNodeAttr)   \
    T(FuncDecl, FuncDecl)               \
    T(VarDecl, VarDecl)                 \
    T(DestructureDecl, DestructureDecl) \
    /* ── ArgNode ── */                 \
    T(ArgNode, ArgNode)                 \
    T(EnumDecl, Sequence)               \
    T(EnumMember, Sequence)             \
    T(StructDecl, Sequence)             \
    T(StructField, Sequence)            \
    /* ── ModuleNode ── */              \
    T(ModuleDecl, ModuleNode)

/** Unified enum for all AST node types (CamelCase).
 *  Generated from PARSER_TOKEN_KINDS — do not edit manually. */
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

// ── Kind → C++ type mapping ──
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

using AstNodePtr = std::shared_ptr<AstNodeBase>;

} // namespace trust