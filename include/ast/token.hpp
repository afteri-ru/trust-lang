// token.hpp - public header for the token library.
// Provides ParserToken::Kind enum for AST nodes only.
// Kind is generated from PARSER_TOKEN_KINDS X-macro.
// To add a new token kind, add an entry to PARSER_TOKEN_KINDS.
// Do NOT modify the enum, name() manually.
//
// Format: T(name, node_type, category)
//   name     - enumerator name (CamelCase)
//   node_type - C++ class that represents this Kind (e.g. Binary, Scope, IdentName)
//   category  - KindCategory: Any / StmtOnly / ExprOnly

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
// Класс-таблица слотов: ЕДИНСТВЕННЫЙ источник перечня классов AST (AST_SLOT_CLASSES).
#include "ast/slots/node_slots_x.hpp"

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
class RawText;
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
// Format: T(name, node_type, category)
//   name      - enumerator name (CamelCase)
//   node_type - C++ class that stores data for this token kind.
//               Multiple kinds can share the same node_type.
//   category  - KindCategory: где узел допустим в кодогене.
//               Any - statement и expression; StmtOnly - только statement-контекст;
//               ExprOnly - только expression-контекст.
//
// Grouped by node_type for readability.
// ============================================================================

/// Контекстная категория kind для кодогена (ast/kind_dispatch.hpp::kindCategory).
enum class KindCategory { Any, StmtOnly, ExprOnly };

#define PARSER_TOKEN_KINDS(T)                                                  \
    /* -- Sequence -- */                                                       \
    T(sequence, Sequence, Any)                                                 \
    T(Attr, Sequence, StmtOnly)                                                \
    /* -- ScopeBlock -- */                                                     \
    T(ScopeBlock, ScopeBlock, StmtOnly)                                        \
    /* -- Binary -- */                                                         \
    T(TypeDecl, Binary, StmtOnly)                                              \
    T(NameDecl, Binary, StmtOnly)                                              \
    T(AssignOp, Binary, Any)                                                   \
    T(AppendStmt, Binary, Any)                                                 \
    T(MathOp, Binary, Any)                                                     \
    T(IncDecOp, Binary, Any)                                                   \
    T(BitwiseOp, Binary, Any)                                                  \
    T(CompareOp, Binary, Any)                                                  \
    T(LogicalOp, Binary, Any)                                                  \
    T(MemberAccess, Binary, Any)                                               \
    T(ArrayAccess, Binary, Any)                                                \
    /* Набор допустимых типов: члены в m_sequence (Sequence::m_body). */       \
    T(TypeSet, Sequence, Any)                                                  \
    /* -- IdentName -- */                                                      \
    T(Ident, IdentName, Any)                                                   \
    /* -- IdentType -- */                                                      \
    T(TypeName, IdentType, Any)                                                \
    /* -- CallExpr -- */                                                       \
    T(CallExpr, CallExpr, Any)                                                 \
    /* -- JumpStmt -- */                                                       \
    T(ReturnStmt, JumpStmt, StmtOnly)                                          \
    T(ThrowStmt, JumpStmt, StmtOnly)                                           \
    /* -- AstNodeAttr -- */                                                    \
    T(Program, AstNodeAttr, StmtOnly)                                          \
    T(VarRef, AstNodeAttr, Any)                                                \
    /* Сырой семантический текст (без нормализации): комментарий/embed C++. */ \
    T(EmbedExpr, RawText, Any)                                                 \
    T(Document, RawText, StmtOnly)                                             \
    T(ContextMacro, ContextMacro, Any)                                         \
    T(CheckAreaStmt, CheckAreaStmt, StmtOnly)                                  \
    T(DebugStmt, DebugStmt, StmtOnly)                                          \
    T(Unimplemented, AstNodeAttr, StmtOnly)                                    \
    T(NotApplicable, AstNodeAttr, StmtOnly)                                    \
    T(IntLiteral, Literal, ExprOnly)                                           \
    T(FloatLiteral, Literal, ExprOnly)                                         \
    T(StrChar, Literal, ExprOnly)                                              \
    T(StrWide, Literal, ExprOnly)                                              \
    T(RationalLiteral, Literal, ExprOnly)                                      \
    T(ArrayInit, DictLiteralNode, ExprOnly)                                    \
    T(DictLiteral, DictLiteralNode, ExprOnly)                                  \
    T(Tuple, DictLiteralNode, ExprOnly)                                        \
    T(RangeExpr, RangeExpr, ExprOnly)                                          \
    T(RefMakeExpr, RefMakeExpr, ExprOnly)                                      \
    T(RefTakeExpr, RefTakeExpr, ExprOnly)                                      \
    T(RefLockExpr, Sequence, ExprOnly)                                         \
    T(RefLockDeref, Sequence, ExprOnly)                                        \
    T(Ellipsis, Sequence, Any)                                                 \
    T(Filling, Sequence, Any)                                                  \
    T(IfStmt, IfStmt, StmtOnly)                                                \
    T(WhileStmt, WhileStmt, StmtOnly)                                          \
    T(AssignmentStmt, AstNodeAttr, StmtOnly)                                   \
    T(SemicolonStmt, SemicolonStmt, StmtOnly)                                  \
    T(LastResultCapture, LastResultCapture, StmtOnly)                          \
    T(ErrorExpr, ErrorExpr, Any)                                               \
    T(BlockStmt, AstNodeAttr, StmtOnly)                                        \
    T(ThenBlock, AstNodeAttr, StmtOnly)                                        \
    T(ElseBlock, AstNodeAttr, StmtOnly)                                        \
    T(DoWhileStmt, DoWhileStmt, StmtOnly)                                      \
    T(WhileElseBlock, AstNodeAttr, StmtOnly)                                   \
    T(BreakStmt, JumpStmt, StmtOnly)                                           \
    T(ContinueStmt, JumpStmt, StmtOnly)                                        \
    T(GotoStmt, LabelRef, StmtOnly)                                            \
    T(LabelStmt, LabelRef, StmtOnly)                                           \
    T(TryCatchStmt, TryCatchStmt, StmtOnly)                                    \
    T(CatchBlock, CatchBlock, StmtOnly)                                        \
    T(MatchingStmt, MatchStmt, StmtOnly)                                       \
    T(WithStmt, WithStmt, StmtOnly)                                            \
    T(MatchingCase, AstNodeAttr, StmtOnly)                                     \
    T(MatchingElseBlock, AstNodeAttr, StmtOnly)                                \
    T(FuncDecl, FuncDecl, Any)                                                 \
    T(VarDecl, VarDecl, StmtOnly)                                              \
    T(ClassDecl, ClassDecl, StmtOnly)                                          \
    T(DestructureDecl, DestructureDecl, StmtOnly)                              \
    /* -- ArgNode -- */                                                        \
    T(ArgNode, ArgNode, ExprOnly)                                              \
    T(EnumDecl, Sequence, StmtOnly)                                            \
    T(EnumMember, Sequence, StmtOnly)                                          \
    T(StructDecl, RecordDecl, StmtOnly)                                        \
    T(StructField, Sequence, StmtOnly)                                         \
    /* -- TrustContract (единая trust-конструкция, kind в поле) -- */          \
    T(TrustContract, TrustContract, StmtOnly)                                  \
    /* -- TrustElem (элемент контрактного программирования) -- */              \
    T(TrustElem, TrustElem, Any)                                               \
    /* -- ModuleNode -- */                                                     \
    T(ModuleDecl, ModuleNode, StmtOnly)

/** Unified enum for all AST node types (CamelCase).
 *  Generated from PARSER_TOKEN_KINDS - do not edit manually. */
struct ParserToken {
    enum class Kind : int {
        END = 0,
#define TOK_ENUM(name, node_type, category) name,
        PARSER_TOKEN_KINDS(TOK_ENUM)
#undef TOK_ENUM
    };

    // Token name
    [[nodiscard]] static constexpr std::string_view name(Kind k) noexcept {
        switch (k) {
        case Kind::END:
            return "END";
#define TOK_NAME(name, node_type, category) \
    case Kind::name:                        \
        return #name;
            PARSER_TOKEN_KINDS(TOK_NAME)
#undef TOK_NAME
        }
        return "<unknown>";
    }
};

/// Число слотов таблиц по kinds: `END` (индекс 0) + все записи `PARSER_TOKEN_KINDS`.
/// Выводится из ЕДИНСТВЕННОГО источника - ручной счёт не ведётся.
inline constexpr std::size_t kParserKindSlotCount = 1 /* END */ +
#define KIND_COUNT_ROW(name, node_type, category) +1
                                                    PARSER_TOKEN_KINDS(KIND_COUNT_ROW);
#undef KIND_COUNT_ROW

/// Индекс kind для таблиц (kind-значения непрерывны от `END = 0`).
[[nodiscard]] constexpr std::size_t kindIndex(ParserToken::Kind k) noexcept {
    return static_cast<std::size_t>(k);
}

// -- Kind → C++ type mapping --
// Each ParserToken::Kind maps to the concrete C++ class that stores its data.

template <ParserToken::Kind K>
struct NodeTypeForKind;

template <ParserToken::Kind K>
using node_type_for_kind_t = typename NodeTypeForKind<K>::type;

#define TOK_NODE_TYPE(name, node_type, category)      \
    template <>                                       \
    struct NodeTypeForKind<ParserToken::Kind::name> { \
        using type = node_type;                       \
    };
PARSER_TOKEN_KINDS(TOK_NODE_TYPE)
#undef TOK_NODE_TYPE

// PARSER_TOKEN_KINDS намеренно НЕ #undef'ится: он переиспользуется в ast/kind_dispatch.hpp
// для генерации kind-визитора (интерфейс + диспетчер). Там же в конце файла он #undef'ится.

// END maps to AstNodeBase (same as any other node without special fields).
template <>
struct NodeTypeForKind<ParserToken::Kind::END> {
    using type = AstNodeBase;
};

/// true, если kind отображается на класс T (единственный источник - PARSER_TOKEN_KINDS).
template <class T>
[[nodiscard]] constexpr bool kindHasNodeType(ParserToken::Kind k) noexcept {
    switch (k) {
#define KIND_NODE_TYPE_ROW(name, node_type, category) \
    case ParserToken::Kind::name:                     \
        return std::is_same_v<node_type, T>;
        PARSER_TOKEN_KINDS(KIND_NODE_TYPE_ROW)
#undef KIND_NODE_TYPE_ROW
    case ParserToken::Kind::END:
        return std::is_same_v<T, AstNodeBase>;
    }
    return false;
}

/// true, если ОБА kind отображаются на ОДИН класс AST. Смена kind допустима только внутри
/// класса (например, DictLiteral -> Tuple): формат хранения и смысл текста при этом не меняются.
[[nodiscard]] constexpr bool sameNodeClass(ParserToken::Kind a, ParserToken::Kind b) noexcept {
    switch (a) {
#define SAME_NODE_CLASS_ROW(name, node_type, category) \
    case ParserToken::Kind::name:                      \
        return kindHasNodeType<node_type>(b);
        PARSER_TOKEN_KINDS(SAME_NODE_CLASS_ROW)
#undef SAME_NODE_CLASS_ROW
    case ParserToken::Kind::END:
        return b == ParserToken::Kind::END;
    }
    return false;
}

// ============================================================================
// Канонический реестр классов AST-узлов (R8).
//
// Архитектурный инвариант (trap из include/ast/MEMORY.md): класс AST - это ФОРМАТ ХРАНЕНИЯ
// данных, а не идентичность узла. РАЗНЫЕ kinds могут отображаться на ОДИН класс (Binary,
// Sequence, ...); отношение kind->класс задаётся полем node_type PARSER_TOKEN_KINDS (третье поле - category).
// ЗАПРЕЩЕНО заводить новый класс под новый kind - нужно переиспользовать существующий.
//
// Канонический перечень классов AST — ПРОИЗВОДНЫЙ: его задаёт класс-таблица слотов
// `AST_SLOT_CLASSES` (ast/slots/node_slots_x.hpp, ключ — класс). Отдельного списка классов нет:
//   - каждый kind маппится на класс ИЗ этого перечня (иначе — ошибка компиляции);
//   - каждый класс перечня используется хотя бы одним kind (нет «мёртвых» классов).
// Расширение перечня допустимо только вместе с настоящей новой сущностью AST-узла (структурой
// данных), а не «на новый kind».
// ============================================================================
// Канонический перечень классов AST — ПРОИЗВОДНЫЙ: его задаёт класс-таблица слотов
// `AST_SLOT_CLASSES` (ast/slots/node_slots_x.hpp, ключ — класс). Отдельного списка классов нет:
// потребители используют `AST_SLOT_CLASSES(M)` с макросом `M(cls, rows)`.

/// Traits: класс AST-узла из канонического списка? Даёт человекочитаемое имя для диагностик.
template <class T>
struct AstNodeClassInfo {
    static constexpr bool kKnown = false;
    static constexpr std::string_view kName = "<not-an-AST-node-class>";
};

#define AST_NODE_CLASS_ENTRY(cls, rows)                 \
    template <>                                         \
    struct AstNodeClassInfo<cls> {                      \
        static constexpr bool kKnown = true;            \
        static constexpr std::string_view kName = #cls; \
    };
AST_SLOT_CLASSES(AST_NODE_CLASS_ENTRY)
#undef AST_NODE_CLASS_ENTRY

#define AST_NODE_CLASS_ASSERT(name, node_type, category)                                                                                      \
    static_assert(AstNodeClassInfo<node_type>::kKnown, "PARSER_TOKEN_KINDS: kind '" #name "' maps to class '" #node_type                      \
                                                       "' which is not in AST_SLOT_CLASSES; reuse an existing AST node class for a new kind " \
                                                       "(do not introduce a new class per kind)");
PARSER_TOKEN_KINDS(AST_NODE_CLASS_ASSERT)
#undef AST_NODE_CLASS_ASSERT

namespace detail {

/// Индекс класса в каноническом списке (для проверки «нет мёртвых классов»).
#define AST_CLASS_ID_ENTRY(cls, rows) k##cls,
enum class AstClassId : std::size_t { AST_SLOT_CLASSES(AST_CLASS_ID_ENTRY) kCount };
#undef AST_CLASS_ID_ENTRY

template <class T>
struct AstClassIdOf;

#define AST_CLASS_ID_SPEC(cls, rows)                            \
    template <>                                                 \
    struct AstClassIdOf<cls> {                                  \
        static constexpr AstClassId value = AstClassId::k##cls; \
    };
AST_SLOT_CLASSES(AST_CLASS_ID_SPEC)
#undef AST_CLASS_ID_SPEC

constexpr std::size_t kAstClassCount = static_cast<std::size_t>(AstClassId::kCount);

/// Массив «класс используется хотя бы одним kind» - заполняется из PARSER_TOKEN_KINDS.
constexpr std::array<bool, kAstClassCount> astClassUsed() noexcept {
    std::array<bool, kAstClassCount> used{};
#define AST_CLASS_MARK(name, node_type, category) used[static_cast<std::size_t>(AstClassIdOf<node_type>::value)] = true;
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
              "AST_SLOT_CLASSES contains a class that no PARSER_TOKEN_KINDS kind maps to (dead AST node class)");

} // namespace detail

using AstNodePtr = std::shared_ptr<AstNodeBase>;

} // namespace trust