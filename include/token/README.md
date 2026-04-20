# Token Library

Unified token identification for all compiler components: Flex (lexer), Bison (parser), AST (nodes). Definitions are auto-generated from `tokens.def` during build. CMake runs a Python script to generate `.gen.*` files from `tokens.def`. `token.hpp` includes them via X-macro pattern.

## Token Format

Each line in `tokens.def` is one `TOKEN(NAME, VALUE, FLAGS)`:

```
TOKEN(END,        0,     FLEX_LEXEME | BISON_TOKEN)
TOKEN(NAME,       AUTO,  FLEX_LEXEME | BISON_TOKEN)
TOKEN(Expr,       AUTO,  BISON_TOKEN | Expr)
TOKEN(IfStmt,     AUTO,  BISON_TOKEN | Stmt)
TOKEN(Program,    999,   Root)
```

### Flags

| Flag | Description |
|------|-------------|
| `FLEX_LEXEME` | Token returned by Flex lexer |
| `BISON_TOKEN` | Participates in Bison grammar |
| `Expr` / `Stmt` / `Decl` | AST node category |
| `Root` | Root AST node |

### Naming Conventions

| Style | Required Flags | Example |
|-------|----------------|---------|
| `UPPER_CASE` | FLEX_LEXEME | `NAME`, `NUMBER` |
| `CamelCase` | at least one AST flag | `Expr`, `IfStmt` |
| `lower_case` | BISON_TOKEN only | `my_term` |
