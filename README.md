# TrustLang — A Trusted Programming Language

TrustLang is a high-level general-purpose programming language focused on safety (trusted) development. It provides memory management without using a garbage collector and detects memory management errors and data race conditions at compile time. The language is implemented as a transpiler to C++, supports deep integration with the C/C++ ecosystem, including mutual function calls, native code embedding, and direct use of the C++ standard library.

The basic syntax of TrustLang is based on strict rules without using keywords, and its extension is implemented through preprocessor macros, which are part of the AST, have limited scope, and are safe to use. The language also has built-in tensor computations using LibTorch and rational numbers with unlimited precision. Static and dynamic typing are supported, named and optional function parameters. Object identifiers allow simultaneous use of file-level modular code organization and regular namespaces.


## Язык коммуникации и комментарии

- Коммуникация может быть на **русском языке**
- Комментарии в коде пишутся только на **английском языке**
- Запрещено использовать Unicode-символы в комментариях (только ASCII)
- Разработка ведется с помощью LLM, поэтому файлы README в каталогах играют роль начальных источников информации для AI агентов.
- **Запрещено читать, писать или выполнять поиск** в каталоге `docs/`, так как он **не является частью кодовой базы**. 

## Подпроекты

| Подпроект | Описание |
|-----------|----------|
| `diag` | Диагностика — сообщения, форматирование, source manager, CLI-опции |
| `parser` | Токенизация (Flex) и парсинг (Bison), генерация токенов из `tokens.def`, MMProc, AST |
| `types` | Система типов — `TypeKind`, `TypeInfo`, совместимость, user-defined типы |
| `gencpp` | Транспайлер — AST-ноды, семантический анализ, symbol table, AST-оптимизации, генерация C++ |
| `runtime` | Среда выполнения и стандартная библиотека — type-erased контейнеры, исключения, подсчёт ссылок, builtin-функции (`print`, `len`, `assert`, математика, коллекции) |
| `cli` | Командная строка — драйвер полного pipeline (lex → parse → analyze → optimize → codegen) |
| `jit` | JIT-компиляция — LLVM ORC JIT, разрешение символов, память выполнения |
| `lsp` | Language Server — autocomplete, go-to-definition, hover-диагностика, форматирование |
| `repl` | Интерактивная оболочка — read-eval-print, автодополнение, инкрементальная JIT |

