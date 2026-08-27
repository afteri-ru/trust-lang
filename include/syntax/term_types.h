#pragma once
#ifndef TRUST_SYNTAX_TERM_TYPES_H_
#define TRUST_SYNTAX_TERM_TYPES_H_

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "location/location.hpp"

namespace trust {

// Forward declarations
class Term;
typedef std::shared_ptr<Term> TermPtr;
// Единое представление последовательности TermPtr: верхнеуровневая sequence
// модуля, вложенные блоки '{ ... }' и тела конструкций - всё хранится в поле
// Term::m_sequence типа SequenceType (см. термин «Sequence» в MEMORY.md).
typedef std::vector<TermPtr> SequenceType;

// TERMS - единый источник всех TermID (терминалы грамматики + AST-узлы).
// Формат: _(NAME, Kind)       - не-терминал грамматики;
//         _(NAME, Kind, T)    - терминал грамматики (генерирует %token).
// Kind - любой ParserToken::Kind (префикс ParserToken::Kind:: добавляет потребитель в term_to_ast.cpp),
// включая Unimplemented (конструкция не реализована: при конвертации - ошибка «не реализовано»,
// узел не строится). Маркер T означает: имя попадает в автогенерируемую секцию %token (кроме END,
// который объявлен отдельно как %token END 0). Имена в TERMS и SYMBOL_TOKENS не дублируются.
#define TERMS(_)                                                          \
    /* -- Не-терминалы: реализованные Kind -- */                          \
    _(NONE, END)                                                          \
    _(SEQUENCE, ScopeBlock)                                               \
    _(BLOCK, ScopeBlock)                                                  \
    _(BLOCK_TRY, ScopeBlock)                                              \
    _(BLOCK_PLUS, ScopeBlock)                                             \
    _(BLOCK_MINUS, ScopeBlock)                                            \
    _(STATIC, Ident)                                                      \
    _(TYPE, TypeName)                                                     \
    _(TYPECAST, TypeName)                                                 \
    _(TYPEDUCK, TypeName)                                                 \
    _(ASSIGN, AssignOp)                                                   \
    _(INDEX, ArrayAccess)                                                 \
    _(FIELD, MemberAccess)                                                \
    _(TENSOR, ArrayInit)                                                  \
    _(DICT, DictLiteral)                                                  \
    _(CLASS, StructDecl)                                                  \
    /* -- Не-терминалы: trust-контракты (единая форма, kind в поле) -- */ \
    _(TRUST_CONTRACT, TrustContract)                                      \
    _(TRUST_ELEM, TrustElem)                                              \
    /* -- Не-терминалы: Unimplemented -- */                               \
    _(TYPENAME, Unimplemented)                                            \
    _(FILLING, Unimplemented)                                             \
    /* -- Не-терминалы: NotApplicable -- */                               \
    _(COMMA_LEXEME, NotApplicable, T)                                     \
    _(MACRO_LEXEME, NotApplicable, T)                                     \
    _(MACRO_STR_LEXEME, NotApplicable, T)                                 \
    _(MACRO_DEL_LEXEME, NotApplicable, T)                                 \
    _(DOCUMENT_INLINE, NotApplicable, T)                                  \
    /* -- Терминалы: маркеры trust-контрактов (в AST не переходят) -- */  \
    _(TRUST_BEGIN, NotApplicable, T)                                      \
    _(TRUST_END, NotApplicable, T)                                        \
    /* -- Терминалы: маркеры терминов решателя (в AST не переходят) -- */ \
    _(TRUST_ELEM_BEGIN, NotApplicable, T)                                 \
    _(TRUST_ELEM_END, NotApplicable, T)                                   \
    /* -- Терминалы: реализованные Kind -- */                             \
    _(ATTRIBUTE, Attr, T)                                                 \
    _(DOCUMENT, Document, T)                                              \
    _(INT_PLUS, ReturnStmt, T)                                            \
    _(INT_MINUS, ThrowStmt, T)                                            \
    _(INT_REPEAT, ContinueStmt, T)                                        \
    _(NAME, Ident, T)                                                     \
    _(LOCAL, Ident, T)                                                    \
    _(MACRO, Ident, T)                                                    \
    _(MODULE, Ident, T)                                                   \
    _(NATIVE, Ident, T)                                                   \
    _(MANGLED, Ident, T)                                                  \
    _(INTEGER, IntLiteral, T)                                             \
    _(NUMBER, FloatLiteral, T)                                            \
    _(COMPLEX, FloatLiteral, T)                                           \
    _(RATIONAL, RationalLiteral, T)                                       \
    _(STRWIDE, StrWide, T)                                                \
    _(STRCHAR, StrChar, T)                                                \
    _(REFLECTION, EmbedExpr, T)                                           \
    _(ARGS, ArgNode, T)                                                   \
    _(ARGUMENT, ArgNode, T)                                               \
    _(MACRO_SEQ, EmbedExpr, T)                                            \
    _(MACRO_STR, EmbedExpr, T)                                            \
    _(MACRO_DEL, EmbedExpr, T)                                            \
    _(MACRO_TOSTR, EmbedExpr, T)                                          \
    _(MACRO_CONCAT, EmbedExpr, T)                                         \
    _(MACRO_ARGUMENT, EmbedExpr, T)                                       \
    _(MACRO_ARGNAME, EmbedExpr, T)                                        \
    _(MACRO_ARGPOS, EmbedExpr, T)                                         \
    _(MACRO_ARGCOUNT, EmbedExpr, T)                                       \
    _(CREATE_TYPE, TypeDecl, T)                                           \
    _(CREATE_NAME, NameDecl, T)                                           \
    _(APPEND, AppendStmt, T)                                              \
    _(SWAP, AssignmentStmt, T)                                            \
    _(FUNCTION, FuncDecl, T)                                              \
    _(COROUTINE, FuncDecl, T)                                             \
    _(ITERATOR, FuncDecl, T)                                              \
    _(OPERATOR_PTR, RefMakeExpr, T)                                       \
    _(FOLLOW, IfStmt, T)                                                  \
    _(WHILE, WhileStmt, T)                                                \
    _(DOWHILE, DoWhileStmt, T)                                            \
    _(MATCHING, MatchingStmt, T)                                          \
    _(WITH, MatchingStmt, T)                                              \
    _(TAKE, RefTakeExpr, T)                                               \
    _(RANGE, RangeExpr, T)                                                \
    _(ELLIPSIS, Ellipsis, T)                                              \
    _(OP_LOGICAL, LogicalOp, T)                                           \
    _(OP_MATH, MathOp, T)                                                 \
    _(OP_COMPARE, CompareOp, T)                                           \
    _(OP_BITWISE, BitwiseOp, T)                                           \
    _(EMBED, EmbedExpr, T)                                                \
    /* -- Терминалы: Unimplemented -- */                                  \
    _(PARENT, Unimplemented, T)                                           \
    _(AWAIT, Unimplemented, T)                                            \
    _(YIELD, Unimplemented, T)                                            \
    _(WHEN_ALL, Unimplemented, T)                                         \
    _(WHEN_ANY, Unimplemented, T)                                         \
    _(NAMESPACE, Unimplemented, T)                                        \
    _(TRUSTLANG, Unimplemented, T)                                        \
    _(ESCAPE, Unimplemented, T)                                           \
    _(ATTR_COMPLETE, Unimplemented, T)                                    \
    _(OPERATOR_DIV, Unimplemented, T)                                     \
    _(OPERATOR_AND, Unimplemented, T)                                     \
    _(OPERATOR_ANGLE_EQ, Unimplemented, T)                                \
    _(OPERATOR_DUCK, Unimplemented, T)                                    \
    _(MACRO_CONTEXT, ContextMacro, T)                                     \
    _(TRY_PLUS_BEGIN, Unimplemented, T)                                   \
    _(TRY_PLUS_END, Unimplemented, T)                                     \
    _(TRY_MINUS_BEGIN, Unimplemented, T)                                  \
    _(TRY_MINUS_END, Unimplemented, T)                                    \
    _(TRY_ALL_BEGIN, Unimplemented, T)                                    \
    _(TRY_ALL_END, Unimplemented, T)                                      \
    _(REPEAT, Unimplemented, T)                                           \
    _(TAKE_CONST, Unimplemented, T)                                       \
    _(ITERATOR_QQ, Unimplemented, T)                                      \
    _(YIELD_BEGIN, Unimplemented, T)                                      \
    _(YIELD_END, Unimplemented, T)
// note: no trailing \ on last line

// SYMBOL_TOKENS - символьные терминалы грамматики (одиночные символы и скобки).
// Формат: _(Name, char) - TermID + bison-токен; участвует в symbolToID/tokenFromID.
// Все записи - терминалы грамматики (генерируют %token).
#define SYMBOL_TOKENS(_) \
    _(LPAREN, '(')       \
    _(RPAREN, ')')       \
    _(LBRACKET, '[')     \
    _(RBRACKET, ']')     \
    _(SEMICOLON, ';')    \
    _(COMMA, ',')        \
    _(DOT, '.')          \
    _(COLON, ':')        \
    _(EQ, '=')           \
    _(PLUS, '+')         \
    _(MINUS, '-')        \
    _(STAR, '*')         \
    _(SLASH, '/')        \
    _(PERCENT, '%')      \
    _(AMP, '&')          \
    _(PIPE, '|')         \
    _(CARET, '^')        \
    _(TILDE, '~')        \
    _(BANG, '!')         \
    _(QUESTION, '?')     \
    _(AT, '@')           \
    _(DOLLAR, '$')       \
    _(LT, '<')           \
    _(GT, '>')           \
    _(LBRACE, '{')       \
    _(RBRACE, '}')

enum class TermID : uint8_t {
    END = 0,
#define DEFINE_ENUM(name, ...) name,
    TERMS(DEFINE_ENUM) SYMBOL_TOKENS(DEFINE_ENUM)
#undef DEFINE_ENUM
};

inline const char* toString(TermID type) {
    switch (type) {
    case TermID::END:
        return "END";

#define DEFINE_CASE(name, ...) \
    case TermID::name:         \
        return #name;
        TERMS(DEFINE_CASE)
        SYMBOL_TOKENS(DEFINE_CASE)
#undef DEFINE_CASE
    default:
        return "UNKNOWN TYPE ";
    }
}

} // namespace trust

#endif // TRUST_SYNTAX_TERM_TYPES_H_
