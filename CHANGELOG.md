# Version history

## [Release v0.6 - current version](https://github.com/afteri/trust-lang/releases/v0.6.0))

## Версия 0.6.x (РАБОЧАЯ) - лексика и архитектура

- Реализован capture «`$^` = результат последней операции» (простой случай): read-only псевдопеременная
  `$^` = значение последнего оператора. Два источника с результатом: (1) голое выражение-оператор
  (напр. `a + 3; r := $^;`, вызов `f(); r := $^;`) — проход lowering (`captureLastResult`) переписывает пару
  в обычный код `__trust_last_N := <expr>;` и заменяет лист `$^` ссылкой на временную (тип выводится штатно,
  значение выполняется один раз); (2) декларация (`x := 1; r := $^;`) — результат уже в `x`, временная не
  нужна, `$^` переписывается прямо на `x`. Неподдержанные обращения `$^` дают точечные явные диагностики
  (единая классификация, без generic-fallback): «no preceding statement to capture» (без источника),
  «no value to capture (void)» (после void-вызова/не-значения), «capture from/after a composite statement …
  not implemented yet» (после составного if/цикл/match/try/блок).

- Стабилизация лексики языка
- Создание рабочего прототипа с ограниченными возможностями для тестирования лексики и общей архитектуры проекта.
- Создание средств разработки (компилятор, LSP сервер, плагин для vscode, песочница для примеров)
- Документирование синтаксиса и архитекутрых решений
- Встроенный контекст-макрос ограничения области `@__CHECK_AREA__(<area> [, <behavior>] [, <attr>...])`
  полностью проводён от исходника до AST: регистрация как контекст-макрос, захват аргументов в
  `GetNextToken` (PragmaEvaluator::evalCheckArea), единый терм-маркер MACRO_CONTEXT, построение узла
  `CheckAreaStmt` в term_to_ast. Семантика валидирует маркер по единому скоуп-стеку (создатели
  областей) и удаляет (кода не даёт); severity: per-macro `default|ignore|warning|error` → `-Wcheck-area`.
  Ветвящиеся конструкции (`if`, `match`) открывают вложенный скоуп как единую область «внутри
  конструкта» (whole-if/whole-match); отдельные `elseif/else/case` из реестра/справки `-Whelp-check-areas`
  убраны до появления per-branch-детекции. Область-маркеры встроены в DSL-макросы `return/break/continue`.
- Реализован RAII-менеджер контекста `with(a=expr, b=expr){...} else {...}`: keyword `with`,
  биндинги-локальные переменные (регистрация в скоупе, -Wshadow/duplicate), else при исключении в
  инициализаторе, автоуничтожение объектов (C++ try/catch + флаг `_wN`, создаваемый lowering).
- Реализован атрибут `@[include("header")@]` (зависимый C++-заголовок нативной декларации/типа)
  и мнемонический макрос `@include("name")`: заголовок эмитится в сгенерированный C++ только
  вместе с объявлением (голое имя → `#include <name>`; аргумент с ведущим `"`/`<` → как есть).
- Реализованы **нативные шаблоны-типы** (`<T> %std::vector() := ...;`): объявление регистрирует
  нативный C++-шаблон-тип при анализе AST; использование `vector<Int32>` эмитит
  `std::vector<int32_t>` + on-use `#include <vector>` (заголовок из `@[include]`). Типовые аргументы -
  голое имя (`Int32`, с мягким warning о сигиле `:`) или явный тип (`:Int32`/вложенные шаблоны).
  Дубль C++-имени со встроенным контейнером (`std::vector`/`std::array` от `:Array`) - мягкая
  диагностика, инстанциация резолвится через встроенный `:Array`. Также: коэрция элемента
  массива-литерала к типу элемента типизированной Array-цели.
- **Тип-алиас на типизированный шаблон**: `copy ::= :vector<Int32>;` → `using c_copy =
  std::vector<int32_t>;`. `::=` принимает ТОЛЬКО тип (тип-имя, шаблон, enum/variant из словаря);
  не-тип в RHS `::=` (`func(...) ::= { }`, `name ::= '123'`, `term ::= term2(arg)`) - ошибка.
  Исправлены тесты, закреплявшие неверное поведение `::=`-с-выражением.
- **Ссылочные типы данных (символьный синтаксис, маркеры ссылки)**: маркер ПЕРЕД именем переменной
  задаёт вид ссылки - `& x : & Int32` → `trust::Shared<int32_t>` (shared), `* u := 10` →
  `std::unique_ptr<int32_t>` (unique), `&? w := & x` → `trust::Weak<...>` (weak); эквивалент
  `@[reftype("...")]`. Маркер ОБЯЗАН быть согласован: `x : &Int32` (только у типа), `& x : Int32`
  (у переменной, тип - значение), `& x : * Int32` (разные) - ошибки. Без явного типа pointee
  выводится из инициализатора (авто-дедукция). В выражении `& shared_var` - взятие слабой ссылки
  (weak из shared), `*ref` - доступ к данным (для shared/weak через lock/Locker, для unique/ptr -
  безопасный `trust::checked_deref(c_u.get())`, бросает на nullptr - БЕЗ UB), `a :=: b` - swap
  (`std::swap`, возвращает `&a`; совместимые типы; `a :=: b` для РАЗНЫХ видов - ошибка с диагностикой
  `unique<...>`/`shared<...>`), `var :=: _` - `std::move(var)`. Обмен ЗНАЧЕНИЙ между разными видами
  ссылок - через `*lhs :=: *rhs` (`std::swap` данных, напр. unique↔shared). Анализатор: контракт
  value-vs-reference (копирование ссылки в значение, weak из unique, арифметика/сравнение над
  ссылками - ошибки с fixit `*<name>`). **LIT-тесты сгруппированы по каталогам**:
  `test/lit/pipeline/e2e/reference/{attribute,declaration,access,swap}/` (символические формы,
  address-of/доступ, `:=:`, `@[reftype]`-атрибут). Примеры `examples/references_shared`,
  `references_swap`, `references_addressof`.
- **Инвариант «нет UNKNOWN-типов после анализатора»**: вид ссылки - часть ТИПА (не отдельный
  маркер-поле); `TypeEmitter::resolveCppTypeId` при `INVALID_TYPE_ID` выводит явную диагностику +
  FAULT (заверение компилятора) вместо тихого вывода `auto` (абстрактные Tuple/Range/Array/Dict
  легитимно дают `auto`).


Отдельные задачи на память:
- полная (end-to-end) реализация PoW-защиты на /run и /download для песочницы или встаивание капчи при необходимости


### Standard features (as in conventional compiled languages)

### Project-specific features



## [Release v0.5](https://github.com/afteri/trust-lang/releases/v0.5.0))

Complete rework of the project's code base with a name change (** NewLang -> TrustLang**).
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

## [Release v0.4 (23.03.2024)](https://github.com/rsashka/newlang/releases/tag/v0.4.0))

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
