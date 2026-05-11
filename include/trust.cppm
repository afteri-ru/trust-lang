module;

#include "diag/location.hpp"
#include "parser/token.hpp"
#include "diag/context.hpp"
#include "types/types.hpp"
#include "gencpp/ast_visitor.hpp"
#include "gencpp/ast.hpp"
#include "gencpp/symbol_table.hpp"
#include "gencpp/ast_builder.hpp"
#include "gencpp/ast_validator.hpp"
#include "gencpp/ast_printer.hpp"
#include "gencpp/cpp_generator.hpp"
#include "gencpp/semantic_analyzer.hpp"
#include "parser/mmproc.hpp"

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
using ::trust::AstTypeTraits;
using ::trust::AstVisitable;
using ::trust::AstVisitor;
using ::trust::BinaryOp;
using ::trust::BinOp;
using ::trust::BlockBody;
using ::trust::BlockItem;
using ::trust::BlockStmt;
using ::trust::CallExpr;
using ::trust::Context;
using ::trust::CppGenerator;
using ::trust::Decl;
using ::trust::DiagnosticEngine;
using ::trust::EnumLiteral;
using ::trust::Expr;
using ::trust::FuncDecl;
using ::trust::FuncSignature;
using ::trust::GeneratorOptions;
using ::trust::IntLiteral;
using ::trust::MapperFile;
using ::trust::MMProcessor;
using ::trust::ParamDecl;
using ::trust::print_ast;
using ::trust::Program;
using ::trust::SemanticAnalyzer;
using ::trust::Severity;
using ::trust::Stmt;
using ::trust::StringLiteral;
using ::trust::SymbolTable;
using ::trust::TypeInfo;
using ::trust::TypeKind;
using ::trust::TypeResolution;
using ::trust::VarDecl;
using ::trust::VarRef;
} // namespace trust