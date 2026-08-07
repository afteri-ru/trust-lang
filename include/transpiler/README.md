# Transpiler — Транспилятор в C++

## Назначение

Генерация C++ кода из AST и SymbolTable после успешного семантического анализа. Преобразует узлы AST (декларации переменных, литералы, type aliases, функции, выражения) в синтаксически корректный C++ код.

## Особенности реализации

- **Единственный метод генерации** — `generateToFile()` записывает C++ код в выходной файл (созданный через `ctx.source().add_output()`) с построением source-map (RangeMap trust→cpp через стек mapStart/mapStop). Строковая генерация в stdout отсутствует.
- **In-process** — транспиляция выполняется внутри процесса компиляции (через Context), без вызова внешнего компилятора.
- **Source-map** — для каждого узла AST создаётся маппинг trust-range → cpp-range; имена переменных, функций, типов и параметров функций дополнительно маппятся через `addNameMapping` для hover-ссылок. Source map встраивается в ELF-секцию `.debug_trust_map` на этапе компиляции C++ (через `ModuleApi::packToMsgpack()`), внешние `.trust` файлы не используются.
- **Экспорт символов** — `exports()` возвращает список `ExportEntry{trustName, cppName}`, собранный в процессе генерации (например для `x := 42` и `%func() ::= {}`).
- **Pipeline position** — расположен после SemanticAnalyzer; не запускается при наличии семантических ошибок.

## Генерируемые конструкции

- `VarDecl` — `std::any x = 42;`, типизированные `x:Int32 := 42` → `int32_t x = 42;`
- `TypeDecl` (`::=`) — `using MyInt = int32_t;`
- `FuncDecl` — сигнатура и тело, forward declaration (`;`), возврат `return expr;`
- Expression statements — присваивания `=`, `+=`, `-=`, `*=`, `/=`, `%=`, целочисленное деление `//` → `static_cast<int64_t>(...) / ...`
- `JumpStmt` — `return expr;`, `throw expr;`
- `IfStmt` — `if (cond) { ... } else if (cond2) { ... } else { ... }`
- `WhileStmt` — `while (cond) { ... }`, с else — `while (cond) { ... } else { ... }`
- `DoWhileStmt` — `do { ... } while (cond);`
- `BreakStmt`/`ContinueStmt` — `goto <метка начала/выхода цикла>;` (безымянные — по ближайшему циклу)
- `MatchStmt` — временная переменная + `if (_m == p1 || _m == p2) { ... } else { ... }`
- Standalone literals и `EmbedExpr` (`{% ... %}`) — выводятся как есть с source mapping