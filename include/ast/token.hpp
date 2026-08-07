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
#include "diag/location.hpp"
#include "utils/error.hpp"

namespace trust {

class AstNodeBase;

// Forward declarations for concrete AST node types (full definitions in their respective headers)
class AstNodeAttr;
class Binary;
class CallExpr;
class Sequence;
class ScopeBlock;
class IdentName;
class IdentType;
class Decl;
class Literal;
class JumpStmt;
class ModuleNode;
class ParamDecl;
class FuncDecl;
class VarDecl;
class IfStmt;
class WhileStmt;
class DoWhileStmt;
class MatchStmt;

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
#define PARSER_TOKEN_KINDS(T)         \
    /* ── Sequence ── */              \
    T(sequence, Sequence)             \
    T(Attr, Sequence)                 \
    /* ── ScopeBlock ── */            \
    T(ScopeBlock, ScopeBlock)         \
    /* ── Binary ── */                \
    T(TypeDecl, Binary)               \
    T(NameDecl, Binary)               \
    T(AssignOp, Binary)               \
    T(MathOp, Binary)                 \
    T(BitwiseOp, Binary)              \
    T(CompareOp, Binary)              \
    T(LogicalOp, Binary)              \
    T(MemberAccess, Binary)           \
    T(ArrayAccess, Binary)            \
    /* ── IdentName ── */             \
    T(Ident, IdentName)               \
    /* ── IdentType ── */             \
    T(TypeName, IdentType)            \
    /* ── CallExpr ── */              \
    T(CallExpr, CallExpr)             \
    /* ── JumpStmt ── */              \
    T(ReturnStmt, JumpStmt)           \
    T(ThrowStmt, JumpStmt)            \
    /* ── AstNodeAttr ── */           \
    T(Program, AstNodeAttr)           \
    T(VarRef, AstNodeAttr)            \
    T(EmbedExpr, AstNodeAttr)         \
    T(IntLiteral, Literal)            \
    T(FloatLiteral, Literal)          \
    T(StringLiteral, Literal)         \
    T(EnumLiteral, AstNodeAttr)       \
    T(ArrayInit, AstNodeAttr)         \
    T(CastExpr, AstNodeAttr)          \
    T(RefMakeExpr, AstNodeAttr)       \
    T(RefTakeExpr, AstNodeAttr)       \
    T(IfStmt, IfStmt)                 \
    T(WhileStmt, WhileStmt)           \
    T(AssignmentStmt, AstNodeAttr)    \
    T(ExprStmt, AstNodeAttr)          \
    T(BlockStmt, AstNodeAttr)         \
    T(ThenBlock, AstNodeAttr)         \
    T(ElseBlock, AstNodeAttr)         \
    T(DoWhileStmt, DoWhileStmt)       \
    T(WhileElseBlock, AstNodeAttr)    \
    T(BreakStmt, JumpStmt)            \
    T(ContinueStmt, JumpStmt)         \
    T(TryCatchStmt, AstNodeAttr)      \
    T(CatchBlock, AstNodeAttr)        \
    T(MatchingStmt, MatchStmt)        \
    T(MatchingCase, AstNodeAttr)      \
    T(MatchingElseBlock, AstNodeAttr) \
    T(FuncDecl, FuncDecl)             \
    T(VarDecl, VarDecl)               \
    /* ── ParamDecl ── */             \
    T(ParamDecl, ParamDecl)           \
    T(EnumDecl, AstNodeAttr)          \
    T(EnumMember, AstNodeAttr)        \
    T(StructDecl, AstNodeAttr)        \
    T(StructField, AstNodeAttr)       \
    /* ── ModuleNode ── */            \
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

#undef PARSER_TOKEN_KINDS

// END maps to AstNodeBase (same as any other node without special fields).
template <>
struct NodeTypeForKind<ParserToken::Kind::END> {
    using type = AstNodeBase;
};

using AstNodePtr = std::shared_ptr<AstNodeBase>;

} // namespace trust