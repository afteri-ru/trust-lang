#pragma once

// Логика макропроцессора, вынесенная из монолита Macro (src/syntax/macro/macro.cpp).
// Свободные функции в namespace trust::syntax; Macro остаётся реестром скоупов и
// тонкими делегирующими методами. Группы: MacroArgParser / MacroMatcher /
// MacroExpander / MacroValidator.

#include "syntax/macro.h"  // Macro, MacroArgsType, MacroMismatch, MacroKind, MacroScope
#include "syntax/term/term.h"   // Term, TermID, SequenceType, MapperRange
#include "syntax/parser.h" // Parser (SkipBrackets / predef() в expandMacros)

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {
namespace syntax {

SequenceType makeMacroId(trust::ParserServices& ctx, const SequenceType& seq);
SequenceType getMacroId(trust::ParserServices& ctx, TermPtr& term);
void insertArg(MacroArgsType& args, std::string name, SequenceType& buffer, size_t pos = static_cast<size_t>(-1));
SequenceType symbolSeparateArg(const SequenceType& buffer, size_t pos, std::vector<std::string> sym, std::string& error);
size_t extractArgs(trust::ParserServices& ctx, SequenceType& buffer, TermPtr& term, MacroArgsType& args);

std::string toMacroHashName(std::string_view str);
bool compareMacroName(std::string_view term_name, std::string_view macro_name);
size_t matchMacro(trust::ParserServices& ctx, const SequenceType& buffer, TermPtr& macro, MacroMismatch* mismatch);
bool identityMacro(trust::ParserServices& ctx, const SequenceType& buffer, TermPtr& macro);
std::string toMacroHash(trust::ParserServices& ctx, TermPtr& term);
TermID markerToken(const Macro& macro, std::string_view key);

SequenceType expandMacros(const TermPtr& macro, MacroArgsType& args, Parser& parser, MapperRange callRange);
std::string expandString(const TermPtr& macro, MacroArgsType& args);
TermPtr getMacroById(Macro& macro, const SequenceType block);
TermPtr getMacro(Macro& macro, std::vector<std::string> list);

bool checkMacro(trust::ParserServices& ctx, const TermPtr& term);
bool bodyHasContract(const Macro& macro, const TermPtr& t);
MacroKind classifyMacro(const Macro& macro, const TermPtr& macro_term);
MacroKind macroKind(const Macro& macro, std::string_view name);
bool isNoParenMacro(const Macro& macro, std::string_view name);
bool isContractMacro(const Macro& macro, std::string_view name);
std::unordered_map<std::string, MacroKind> macroKinds(const Macro& macro);

} // namespace syntax
} // namespace trust
