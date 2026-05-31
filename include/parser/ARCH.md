# Parser Architecture

## Pipeline

```
Source → Lexer → LexemeSequence → MMProcessor → TokenSequence → ParserAST → AstNodeSequence
```

## Components

### Lexer

- The Flex-based tokenizer breaks the source code into tokens.
- Processes full source at once (it's not a callback to the Bison parser) and produces a `LexemeSequence` as a result.
- The `Lexeme` is a `string_view` of a fragment of the input token string and its position in the source file (`Location`).
- If errors occur, the analysis stops immediately and a diagnostic message from `diag`.

### MMProcessor

- Runs before `ParserAST` and creates `TokenSequence` from `LexemeSequence` (lexer output for parser input).
- Expands macros and handles module loading.
- Concatenates consecutive string/embed tokens and applies unescape to regular strings.
- Converts identifiers (NAME, LOCAL, NATIVE, NAMESPACE) into unified `Ident` tokens.
- All other `Lexeme` are converted to `TokenInfo` with a range of positions and `std::string` text.
- Errors do not interrupt processing but accumulate in `diag`.

### ParserAST (Bison)

- Receives a `TokenSequence` and, by analyzing `TokenInfo::kind` (`ParserToken::Kind`), creates an AST from the token sequence.
- Analyzes the remaining grammar: expressions, operators, declarations to build the AST.
- Some tokens serve only a syntactic function (delimiters, parentheses, punctuation) and do not create AST nodes.
- `TokenInfo`- a universal structure for the semantic AST, with separate storage for different data, namespace, sequence, expression, etc.

## Token Categories (`ParserToken::Kind`)

- **Flex tokens** (UPPER_CASE): emitted by lexer, consumed by Bison as `%token`
- **TokenInfo** (CamelCase): Bison `%nterm`, produced by grammar actions
- **Bison-only** (lower_case): internal grammar symbols
