#pragma once

// ЕДИНЫЙ источник строк-глифов операторов TrustLang. Таблицы `ast/binary_op.hpp`
// (enum-opcode <-> глиф) и `types/call/operator_registry.hpp` (глиф <-> dunder/C++-конструкция) —
// проекции над этими константами. Заголовок листовой (только <string_view>), доступен обоим слоям.

#include <string_view>

namespace trust::spell {

// Арифметика.
inline constexpr std::string_view kAdd = "+";
inline constexpr std::string_view kSub = "-";
inline constexpr std::string_view kMul = "*";
inline constexpr std::string_view kDiv = "/";
inline constexpr std::string_view kIntDiv = "//";
inline constexpr std::string_view kMod = "%";

// Составное присваивание.
inline constexpr std::string_view kAddAssign = "+=";
inline constexpr std::string_view kSubAssign = "-=";
inline constexpr std::string_view kMulAssign = "*=";
inline constexpr std::string_view kDivAssign = "/=";
inline constexpr std::string_view kIntDivAssign = "//=";
inline constexpr std::string_view kModAssign = "%=";
inline constexpr std::string_view kShlAssign = "<<=";
inline constexpr std::string_view kShrAssign = ">>=";
inline constexpr std::string_view kAndAssign = "&=";
inline constexpr std::string_view kOrAssign = "|=";
inline constexpr std::string_view kXorAssign = "^=";

// Присваивание/определение/прочее.
inline constexpr std::string_view kAssign = "=";
inline constexpr std::string_view kSwap = ":=:";
inline constexpr std::string_view kTypeIsA = "<~";
inline constexpr std::string_view kTypeDuck = "~~";
inline constexpr std::string_view kTypeStrict = "~~~";
inline constexpr std::string_view kLogicalAnd = "&&";
inline constexpr std::string_view kLogicalOr = "||";
inline constexpr std::string_view kLogicalXor = "^^";
inline constexpr std::string_view kDeclare = ":=";
inline constexpr std::string_view kTypeCreate = "::=";
inline constexpr std::string_view kTypeAlias = "::-";
inline constexpr std::string_view kAppend = "[]=";
inline constexpr std::string_view kMember = ".";
inline constexpr std::string_view kIndexOpen = "[";

// Сравнения.
inline constexpr std::string_view kEq = "==";
inline constexpr std::string_view kEqExact = "===";
inline constexpr std::string_view kNe = "!=";
inline constexpr std::string_view kNeExact = "!==";
inline constexpr std::string_view kNeNoCast = "=/=";
inline constexpr std::string_view kLt = "<";
inline constexpr std::string_view kGt = ">";
inline constexpr std::string_view kLe = "<=";
inline constexpr std::string_view kGe = ">=";

// Бинарные (точечная нотация).
inline constexpr std::string_view kBitAnd = ".&.";
inline constexpr std::string_view kBitOr = ".|.";
inline constexpr std::string_view kBitXor = ".^.";
inline constexpr std::string_view kBitNot = ".~.";
inline constexpr std::string_view kShl = ".<.";
inline constexpr std::string_view kShr = ".>>.";
inline constexpr std::string_view kShrAlt = ".>.";

// Операторные символы перегружаемых операторов.
inline constexpr std::string_view kCall = "()";
inline constexpr std::string_view kIndex = "[]";
inline constexpr std::string_view kInc = "++";
inline constexpr std::string_view kDec = "--";

} // namespace trust::spell
