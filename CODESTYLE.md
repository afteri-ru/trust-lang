# TrustLang Coding Style

## 1. Naming

| Entity | Pattern | Example |
|---|---|---|
| Macro | `ALL_CAPS` | `ASSERT_NOT_NULL` |
| Class, struct, type alias | PascalCase | `TokenStream` |
| Function, method | camelCase | `parseToken` |
| Local variable | snake_case | `token_count` |
| Member field | `m_snake_case` | `m_token_list` |
| Static member | `s_snake_case` | `s_instance` |
| Global variable | `g_snake_case` | `g_verbosity` |
| Constant | `kPascalCase` | `kMaxTokens` |
| `enum class` value | `PascalCase` | `Integer` |
| Plain `enum` value | `ALL_CAPS` | `TOKEN_EOF` |
| Namespace | snake_case | `trust::parser` |
| File | snake_case ≤15 chars | `token_info.hpp` |

Identifiers must be self-documenting. Function names must contain a verb.

### Exceptions to naming rules and automatically generated names

Naming rules do not apply to automatically generated files (such as parser.yy.*, lexeryy.*).

Enumerations in ParserToken, TokenFlag, and TokenCategory use their own naming rules.

### Automatically generated identifier names have their own prefixes

"c_" — a synonym identifier for the `extern  "C"`
"cpp_" — a synonym identifier for the `extern "C++"`
"ru_" — a transliteration of the Russian identifier using ASCII characters
"u8_" — an identifier encoding in UTF-8 using ASCII characters
"tr_" — a synonym for the Trust-lang identifier in C++

## 2. Formatting (enforced by `.clang-format`)

- Opening brace on the same line as the statement (Attach).
- Braces mandatory for all blocks — even single statements.
- Indent: 4 spaces, no tabs.
- Column limit: 160.
- Space before parentheses: never.
- Space after `template` keyword, no spaces inside `<>`.
- `*` and `&` align left.
- Constructor initializers: break before colon, one per line.
- Include order in `.cpp`: the matching header first, then alphabetically.
- Max empty lines: 1.
- Comments: English only, ASCII.

## 3. Prohibited

- `using namespace std` / `clang` / `llvm`.
- `using` in header files.
- `#define` for constants — use `constexpr` or `enum : type` with `k` prefix for all elements.
- Ternary operators.
- C-style casts.
- Multiple function calls in conditions — store in temporaries.
- Chained calls — break into separate statements.
- `size()` in loop condition on unchanged container.

## 4. Required

- `explicit` for single-argument constructors.
- `override` for overridden virtual functions.
- `nullptr` for empty pointers.
- `auto` only when type is deducible.
- Forward declarations in headers when sufficient.
- Lambdas only directly at the call site.

## 5. Project Structure

| Type | Extension | Directory |
|---|---|---|
| C++ header | `.hpp` | `include/<component>/` |
| C header | `.h` | `include/<component>/` |
| Implementation | `.cpp` | `src/<component>/` |
| Module | `.cppm` | `include/` |
| Unit test | `.cpp` | `test/unit/` |
| Lit test | `.src` | `test/lit/` |

Header guard: `#pragma once`.

## 6. Strict compiler flags (in `CMakeLists.txt`)

C++ standard ≥ 20 (currently 23).

```
-Werror -Wold-style-cast -Wreorder -Wnon-virtual-dtor -Woverloaded-virtual 
```

- **System/external headers** (LLVM, msgpack, GMP, LIBTORCH, LLDB): included via `SYSTEM` (`-isystem`), so `-Werror` does not apply to them.
- **Generated files** (`lex.yy.cpp`): use `-Wno-old-style-cast`
- **Test binary** additionally uses `-Wno-unused-result`

## 7. Static Analysis (`.clang-tidy`)

- `modernize-use-auto`
- `cppcoreguidelines-no-malloc`
- `google-build-using-namespace`
- `bugprone-reserved-identifier`
- `misc-redundant-expression`
- `readability-identifier-naming`
