# Parser Architecture

## Pipeline

```
Source → Lexer → LexemeSequence → MMProcessor → AstNodeSequence → ParserAST → FULL AST
```

## Components

### Lexer

- The Flex-based tokenizer breaks the source code into tokens.
- Processes full source at once (it's not a callback to the Bison parser) and produces an `LexemeSequence` as a result.
- The `Lexeme` is a `string_view` of a fragment of the input token string and its position in the source file (`SourceLoc`).
- If errors occur, the analysis stops immediately and a diagnostic message from `diag`.

### MMProcessor

- Run before `ParserAST` and create `AstNodeSequence` from `LexemeSequence` (lexer output for parser input)
- Expands macros and handles module loading
- `MMProcessor` converts certain token categories directly to AST nodes (strings, embeds, identifiers)
- All other `Lexeme` convert to `TokenInfo` with a range of positions (beginning and end of the fragment) and `std::string` from a `string_view`.
- Errors do not interrupt processing but accumulate in `diag`


### ParserAST (Bison)

- Input: `AstNodeSequence`, where each `AstNode` contains `TokenInfo` with `ParserToken::Kind` - the `Lexeme` code
- Analyzes the remaining grammar: expressions, operators, declarations, and builds an AST.
- Some tokens serve only syntactic purpose (separators, brackets, punctuation) and do not produce AST nodes
- The `ParserAST` creates `AstNode` or moves them from the input `AstNodeSequence` and appends them to `out`.

## Token Categories (`ParserToken::Kind`)

- **Flex tokens** (UPPER_CASE): emitted by lexer, consumed by Bison as `%token`
- **AST nodes** (CamelCase): Bison `%nterm`, produced by grammar actions
- **Bison-only** (lower_case): internal grammar symbols
