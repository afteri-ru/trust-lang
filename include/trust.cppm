module;

#include "parser/token.hpp"
#include "diag/location.hpp"
#include "diag/context.hpp"
#include "types/type_info.hpp"
#include "gencpp/ast_visitor.hpp"
#include "gencpp/ast.hpp"
#include "gencpp/symbol_table.hpp"
#include "gencpp/ast_builder.hpp"
#include "gencpp/ast_validator.hpp"
#include "gencpp/ast_printer.hpp"
#include "gencpp/cpp_generator.hpp"
#include "gencpp/semantic_analyzer.hpp"

#include <sstream>
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <memory>

export module trust;

export namespace trust {
  // Re-export types from headers into the module namespace
  using ::trust::ParserToken;
  using ::trust::TokenCategory;
  typedef ::trust::TokenCategory NodeCategory;
  using ::trust::TypeKind;
  using ::trust::TypeInfo;
  using ::trust::Program;
  using ::trust::Expr;
  using ::trust::Stmt;
  using ::trust::Decl;
  using ::trust::AstVisitable;
  using ::trust::AstVisitor;
  using ::trust::IntLiteral;
  using ::trust::StringLiteral;
  using ::trust::VarRef;
  using ::trust::BinaryOp;
  using ::trust::CallExpr;
  using ::trust::EnumLiteral;
  using ::trust::VarDecl;
  using ::trust::FuncDecl;
  using ::trust::ParamDecl;
  using ::trust::BinOp;
  using ::trust::SourceLoc;
  using ::trust::SourceIdx;
  using ::trust::SourceRange;
  using ::trust::TypeResolution;
  using ::trust::StringUtils;
  using ::trust::BinOpUtils;
  using ::trust::BlockItem;
  using ::trust::BlockBody;
  using ::trust::BlockStmt;
  using ::trust::print_ast;
  using ::trust::CppGenerator;
  using ::trust::GeneratorOptions;
  using ::trust::OutputFormat;
  using ::trust::AstTypeTraits;
  using ::trust::Context;
  using ::trust::DiagnosticEngine;
  using ::trust::Severity;
  using ::trust::SymbolTable;
  using ::trust::FuncSignature;
  using ::trust::SemanticAnalyzer;
}