# Version history

## [Release v0.5 - current version](https://github.com/afteri/trust-lang/releases/v0.5.0)

Complete rework of the project's code base with a name change (**NewLang -> TrustLang**).
This release is the first after the switch to agent-driven development and is intended
to capture the state after a fundamental architecture refactoring.

Currently only a minimal set of language instructions is implemented (variable and
function declarations, built-in code blocks, etc.), as the main focus is on building
a full ecosystem: a VS Code plugin, and LSP and DAP servers for code debugging.

### Standard features (as in conventional compiled languages)
- Transpilation to C++ followed by build via generated Makefile.
- Lexical and syntax analysis using Flex/Bison.
- Macroprocessor and DSL macros.

### Project-specific features
- **Transparent display of generated code fragments:** a built-in source map (trust <-> C++)
  embedded into the ELF `.debug_trust_map` section; LSP and DAP allow navigation between
  the source trust code and the generated C++, and setting variable values in both files.
- **VS Code extension:** syntax highlighting and LSP integration.
- **LSP server (`trust-lsp`):** Go to Definition, hover, inlay hints, document links,
  incremental synchronization and debounce.
- **DAP server (`trust-dap`):** prototype debugging via GDB/MI with trust <-> C++ position translation.
- **Solver (SMT-LIB 2):** prototype formula generation and optional Z3 integration for formal
  verification (Trust Checking).


## [Release v0.4 (23.03.2024) - current version](https://github.com/rsashka/newlang/releases/tag/v0.4.0))

## New features and changes in the syntax of NewLang v0.4
- Reworked the definition of object types using [prefix naming (sigils)](https://newlang.net/docs/syntax/naming/)
- Interrupting the execution flow and returning can now be done for [named code blocks](https://newlang.net/docs/ops/throw/).
- Simplified the syntax for importing [native variables and functions (C/C++)](https://newlang.net/docs/types/native/)
- Stabilized the syntax for [initializing tensor, dictionary, and function argument values](https://newlang.net/docs/ops/create/#comprehensions) with initial data.
- Added built-in macros for writing code using keywords in a [DSL style](https://newlang.net/docs/syntax/dsl/)

## New compiler features (nlc)
- Completely redesigned the macroprocessor.
- Reworked the compiler architecture with division into parser, macroprocessor, syntax analyzer, interpreter, and code generator.

## Miscellaneous
- The documentation [website](http://newlang.net) has been translated to [Hugo](https://gohugo.io/) and made bilingual.
- Instead of binary builds, a section [Playground and example code](https://newlang.net/playground/) has been added to the website for small experiments.
- Transition to clang-16 has been completed (transition to clang-17 and newer is planned after full implementation of coroutines and support for extended floating-point number formats).
- The number of project contributors has increased to more than one!

------

## [Релиз v0.3 (07.11.2022)](https://github.com/rsashka/newlang/releases/tag/v0.3.0)

### Новые возможности и изменения в синтаксисе NewLang v0.3

- Простые чистые функции удалены.
- Зафиксирован синтаксис операторов проверки [условия](https://newlang.net/ru/ops.html#условный-оператор) и [циклов](https://newlang.net/ru/ops.html#операторы-циклов). 
- Оператор цикла **while** теперь поддерживает конструкцию [**else**](https://newlang.net/ru/ops.html#операторы-циклов).
- В синтаксис NewLang добавлены [пространства имен](https://newlang.net/ru/syntax.html#пространства-имен).
- Реализована часть концепции ООП и добавлена поддержка [определения классов](https://newlang.net/ru/type_oop.html).
- Переработана идеология [возвратов из функции и обработки исключений](https://newlang.net/ru/newlang_doc.html#операторы-прерывания-выполнения-оператор-возврата).

### Разное версии 0.3

- Выполнен переход на clang 15
- Реализован вызов функций с помощью libffi
- Сделана полноценная поддержка Windows

------

## [Релиз 0.2 (11.08.2022)](https://github.com/rsashka/newlang/releases/tag/v0.2.0)

### Новые возможности и изменения в синтаксисе NewLang 0.2

- Добавлены макросы (появилась возможность использовать более привычный синтаксис на основе ключевых слов)
- Реализованы итераторы
- Добавлен новый тип данных - рациональные числа не ограниченной точности
- Многострочные комментарии стали вложенными
- Имена встроенных типов переименованы с указанием размерности

### Другие важные изменения в версии 0.2

- Вместо использования gcc перешел на clang, а libffi замененил на JIT компиляцию вызова для нативных функций
- В релиз добавлены бинарные сборки для Linux
- Начало портирования кода на Windows

------

## [Релиз 0.1 (24.06.2022) - первая публичная версия](https://github.com/rsashka/newlang/releases/tag/v0.1.0)

- Представление общей концепции языка
- Сборка тестов и примеров под Linux из исходников
