#pragma once
#ifndef TRUST_SYNTAX_TERM_TYPES_H_
#define TRUST_SYNTAX_TERM_TYPES_H_

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "diag/location.hpp"

namespace trust {

// Forward declarations
class Term;
typedef std::shared_ptr<Term> TermPtr;
typedef std::vector<TermPtr> BlockType;

#define NL_TERMS(_)   \
    _(NONE)           \
    _(ATTRIBUTE)      \
                      \
    _(DOC_BEFORE)     \
    _(DOC_AFTER)      \
                      \
    _(SEQUENCE)       \
    _(BLOCK)          \
    _(BLOCK_TRY)      \
    _(BLOCK_PLUS)     \
    _(BLOCK_MINUS)    \
                      \
    _(INT_PLUS)       \
    _(INT_MINUS)      \
    _(INT_REPEAT)     \
                      \
    _(NAME)           \
    _(LOCAL)          \
    _(STATIC)         \
    _(MACRO)          \
    _(MODULE)         \
    _(NATIVE)         \
    _(MANGLED)        \
                      \
    _(TYPE)           \
    _(INTEGER)        \
    _(NUMBER)         \
    _(COMPLEX)        \
    _(RATIONAL)       \
                      \
    _(STRWIDE)        \
    _(STRCHAR)        \
    _(TEMPLATE)       \
    _(REFLECTION)     \
                      \
    _(ARGS)           \
    _(ARGUMENT)       \
    _(TRUSTLANG)      \
    _(TYPENAME)       \
    _(TYPECAST)       \
    _(TYPEDUCK)       \
    _(UNKNOWN)        \
    _(SYMBOL)         \
    _(NAMESPACE)      \
    _(PARENT)         \
    _(ESCAPE)         \
                      \
    _(MACRO_SEQ)      \
    _(MACRO_STR)      \
    _(MACRO_DEL)      \
    _(MACRO_TOSTR)    \
    _(MACRO_CONCAT)   \
    _(MACRO_ARGUMENT) \
    _(MACRO_ARGNAME)  \
    _(MACRO_ARGPOS)   \
    _(MACRO_ARGCOUNT) \
    _(LBRACE)         \
    _(RBRACE)         \
    _(CREATE_TYPE)    \
    _(CREATE_NAME)    \
    _(ASSIGN)         \
    _(APPEND)         \
    _(SWAP)           \
                      \
    _(FUNCTION)       \
    _(COROUTINE)      \
    _(ITERATOR)       \
    _(OPERATOR_PTR)   \
                      \
    _(FOLLOW)         \
    _(WHILE)          \
    _(DOWHILE)        \
    _(MATCHING)       \
    _(WITH)           \
    _(TAKE)           \
                      \
    _(AWAIT)          \
    _(YIELD)          \
    _(WHEN_ALL)       \
    _(WHEN_ANY)       \
                      \
    _(RANGE)          \
    _(ELLIPSIS)       \
    _(FILLING)        \
                      \
    _(INDEX)          \
    _(FIELD)          \
                      \
    _(TENSOR)         \
    _(SET)            \
    _(DICT)           \
    _(CLASS)          \
    _(OP_LOGICAL)     \
    _(OP_MATH)        \
    _(OP_COMPARE)     \
    _(OP_BITWISE)     \
    _(EMBED)
// note: no trailing \ on last line

enum class TermID : uint8_t {
    END = 0,
#define DEFINE_ENUM(name) name,
    NL_TERMS(DEFINE_ENUM)
#undef DEFINE_ENUM
};

inline const char* toString(TermID type) {
    switch (type) {
    case TermID::END:
        return "END";

#define DEFINE_CASE(name) \
    case TermID::name:    \
        return #name;
        NL_TERMS(DEFINE_CASE)
#undef DEFINE_CASE

    default:
        return "UNKNOWN TYPE ";
    }
}

/**
 * Единый тип значения для передачи между правилами грамматики (api.value.type).
 * Теперь это просто TermPtr.
 */
using ParseValue = TermPtr;

/**
 * Тип буфера для последовательности лексем/нетерминалов.
 * Используется в макросах и для временных последовательностей.
 */
using MacroBuffer = BlockType;

} // namespace trust

#endif // TRUST_SYNTAX_TERM_TYPES_H_
