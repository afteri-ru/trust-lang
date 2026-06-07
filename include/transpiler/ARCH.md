# C++ Transpiler Architecture

## Purpose
Generates C++ code fragments from validated AST and symbol table.
Called after semantic analysis succeeds.

## Scope (Phase 1)
- Variable declarations with initialization → `auto x = 42;`
- Literals → C++ literal syntax
- Type aliases → `using MyInt = int32_t;`

## Pipeline Position
```
Parser → SemanticAnalyzer → [on success] → CppTranspiler
```

## Components

### CppTranspiler
Walks the AST and produces C++ code directly to a file (no string-based generation).

**Constructor:** `explicit CppTranspiler(Context& ctx)` — единственный. Использует `ctx.symbols()` и `ctx.types()` для доступа к таблице символов и реестру типов.

**Методы:**
- `generateToFile(ast_nodes, output_idx)` — единственный метод генерации. Записывает C++ код в выходной файл с построением source map через `mapStart/mapStop`. Для каждого узла AST создаётся RangeMap input (trust) → output (cpp).
- `exports()` — возвращает список `ExportEntry{trustName, cppName}`, собранных в процессе генерации (например для `type Byte = Int8`).

**Приватные методы:**
- `generateVarDeclToFile()` — генерация объявления переменной (`:=`)
- `generateTypeDeclToFile()` — генерация объявления типа (`::=`)
- `generateExpr()` — генерация выражения (возвращает строку с C++ кодом)
- `generateExprStmtToFile()` — генерация statement-level выражения

**Маппинг:** Для каждого узла AST создаётся RangeMap input (trust) → output (cpp) через стек mapStart/mapStop в Context. Имена объявлений (переменных, функций, типов) и параметров функций дополнительно регистрируются через `addNameMapping` для hover-ссылок (trust → cpp).

**VarDecl и `:=`:** терм оператора `:=` (CREATE_NAME) несёт range только оператора; в `term_to_ast` его `m_mapperRange` расширяется до `[left.begin, right.end]` (вся строка, если name и initializer в одном файле), чтобы `mapStart` покрывал весь statement. Для точного name-маппинга `VarDecl::nameRange()` возвращает диапазон реального имени из базового `m_term->m_left` (не дублирующее поле) — он используется в `generateVarDeclToFile` вместо вывода из `range().begin` (важно при макро-раскрытии, где range() может указывать на оператор).

**Бинарные statement-операторы (`+=`, `-=`, `=`, `//=` и т.п.) и `:=`:** терм оператора (напр. `+=` из `logical operator arithmetic`) несёт range только оператора. Общий хелпер `expandTermRangeToChildren` (в `term_to_ast`) расширяет `m_mapperRange` до `[left.begin, right.end]` для всех составных (бинарных) узлов — вызывается и в ветке `CREATE_NAME` (`:=`), и в `makeNodeForKind` для `is_binary_kind`. Guard одинаков: только когда left/right валидны, в одном файле и begin <= end. Это нужно, чтобы `generateExprStmtToFile`/`mapStart` покрывал весь statement, а trust-lsp подсвечивал/наводил на всю строку, а не только на оператор.
**FuncDecl (`::=` + NATIVE-lval):** сигнатура и тело маппятся раздельно. Диапазон statement'а функции — `[m_left.begin, оператор.end]` (имя + оператор, БЕЗ тела): в ветке `CREATE_TYPE && m_left->NATIVE` (`term_to_ast`) `m_mapperRange` сужается до левой границы из `m_left` и конца оператора (в отличие от `expandTermRangeToChildren`, который охватил бы и тело). В `generateFuncDeclToFile` этим диапазоном маппится сигнатура (`void func(...)`), а тело (`{ ... }`) — отдельным `mapStart/mapStop` из `FuncDecl::blockRange()` (`m_term->m_right`), чтобы скобки отображались в C++. Имя функции регистрируется name-маппингом из `range().begin` (теперь указывает на начало имени `%func`).

**Embed-блоки (`{% ... %}`):** range EMBED-токена покрывает только содержимое между разделителями (не `{%`/`%}`). Конкатенация EMBED-блоков в парсере убрана: каждый `{% ... %}` — отдельный узел (`EmbedExpr`) со своим range (правило `sequence: sequence EMBED` позволяет соседним блокам без `;` быть отдельными seq_item'ами).

**Переводы строк между блоками (все блоки, не только EMBED):** перенос строки между последовательными узлами в C++ управляется `emitBlockSeparator(prev, node, output_idx)`, вызываемым в циклах обхода (top-level `generateToFile`, `sequence`/`ScopeBlock`/`ModuleNode`, тело функции `body_gen`). `'\n'` вставляется между блоками ТОЛЬКО если строка конца `prev` != строки начала `node` (`source().line()`). Если блоки на одной строке исходника — перевод строки не выводится, а между ними ставится пробел для читаемости (`emitSameLineSpace`), но только если на границе нет пробельного символа (не дублирует пробелы из EMBED-содержимого). Хвостовых `\n` в отдельных функциях генерации нет — перевод строки расставляется на границах блоков, поэтому внутреннее форматирование сохраняется корректным.

**Зеркалирование раскладки исходника внутри функции:** тело функции (блок `{ ... }`) также повторяет строки исходника — `'{'`/`'}'` и операторы размещаются по строкам исходника. Диапазон блока берётся из `FuncDecl::blockRange()` (`m_term->m_right`); для непустого тела строки `{`/`}` сравниваются с первым/последним оператором (`emitBlockSeparator`-логика). Для пустого тела диапазон блока покрывает только `{` (не `}`), поэтому `{` и `}` всегда разделяются переводом строки (`{\n}`).

### Source Map
Source map генерируется и встраивается в ELF-секцию `.debug_trust_map` на этапе компиляции C++.
Содержит:
- forward mapping: trust → cpp
- backward mapping: cpp → trust
- name mappings
- хеши файлов для верификации

## Changes (June 2026)
- SymbolTable перемещена из SemanticAnalyzer в Context (доступна через `ctx.symbols()`)
- CppTranspiler принимает Context& вместо SymbolTable&
- Метод `generate()` удалён — только `generateToFile()`
- Добавлен `generateExpr()` для правой части выражений (возвращает строку)
- Source map встраивается в ELF (`ModuleApi::packToMsgpack()`), внешние `.trust` файлы не используются
- `ExportEntry` — структура для экспортированных символов (trustName → cppName)
- `generateExprStmtToFile()` — для statement-level выражений (присваивание, ++, --)
- `generateTypeDeclToFile()` — для объявлений типов (`::=`)
