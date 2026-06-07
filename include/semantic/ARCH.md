# Semantic Analyzer Architecture

## Purpose
Semantic analyzer walks the AST produced by the parser, builds a symbol table,
and performs consistency checks. Reports errors via `diag`.

## Scope (Phase 1)
- Variable declarations with initialization (`x := 42;`, `x : Int32 := 42;`)
- Literals in expressions
- Validation: duplicate names, undefined names, undefined types, missing initializer

## Pipeline Position
```
Parser → SemanticAnalyzer → [on success] → CppGenerator
```

## Components

### SymbolTable
Stores all declared names with their types and source positions.
Holds a `Context*` back-reference (set at construction) for error reporting via `m_ctx->diag()`.
SymbolTable is owned by `Context` (`ctx.symbols()`), not by SemanticAnalyzer.

### SemanticAnalyzer
Recursive AST walker. For each `BinaryOp` with `:=` operator:
- Register the left-hand side name
- If right-hand side is an `Ident`, look it up in the symbol table
- If left-hand side has a type annotation (`:TypeName`), validate the type exists

Extended analysis (beyond Phase 1):
- `analyzeVarDecl()` — variable declarations (`:=`) with type/name validation
- `analyzeTypeDecl()` — type aliases (`::=`) with type resolution
- `analyzeFuncDecl()` — function declarations with parameter and return type checks
- `walkExprForIdents()` — recursive expression walk to find Ident references
- `lookupOrError()` — symbol lookup with diagnostic on failure
