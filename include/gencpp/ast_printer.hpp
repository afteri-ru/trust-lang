#pragma once

#include "gencpp/ast_visitor.hpp"
#include <string>

namespace trust {

// Forward declarations
class Program;

// AST-Printer: обходит AST и печатает в текстовом AST-формате
// Вынесен из ast_format_parser.cpp для уменьшения связности
std::string print_ast(const Program *program);

} // namespace trust