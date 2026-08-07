# C++ Transpiler Architecture

## Purpose
Generates C++ code fragments from validated AST and symbol table.
Called after semantic analysis succeeds.

## Scope (Phase 1)
- Variable declarations with initialization → `auto x = 42;`
- Literals → C++ literal syntax
- Type aliases → `using MyInt = int32_t;`
- Control flow: `if`/`else-if`/`else` → `if (...) { ... } else if (...) { ... } else { ... }`; `while`/`while-else` → `while (...) { ... } else { ... }`; `do-while` → `do { ... } while (...);`
- Jump statements: `return expr;`, `throw expr;`, void `return;`
- `break`/`continue` → `goto <метка начала/выхода цикла>;`
- `match` → временная переменная + `if/else-if/else`

## Единая раскладка control-flow в Term (parser.y)
`FOLLOW`/`WHILE`/`DOWHILE` используют одинаковые роли полей:
- `m_left`  — условие (`cond`) — всегда;
- `m_right` — `else` (для if/while; у do-while пуст) — "else известен сразу";
- `m_block` — тело в `[0]`; для `elseif` — branch-термы с `[1..]` (каждый `m_left`=cond, `m_right`=body).
Полный диапазон statement'а вычисляется в term_to_ast (`expandControlFlowRange`): от min-begin до max-end по детям
(для do-while это `[body.begin, cond.end]` — текстовый порядок «тело→условие» сохраняется в range, а не в полях).

## Интеррапты: break / continue / return / throw (parser.y `exit_part`)
Все операторы прерывания — `exit_part: interrupt | interrupt rval interrupt`, где
`interrupt: INT_PLUS | INT_MINUS | INT_REPEAT`. Роль определяется по TermID и наличию значения:
- `INT_PLUS` (`++`) без значения → **break**; со значением → **return**.
- `INT_REPEAT` (`-+` / `+-`) → **continue**.
- `INT_MINUS` (`--`) → **throw**.
- Void-return записывается явно `++ _ ++` (значение — служебный символ `_`, `m_value = nullptr`).
Имя блока (label) из `exit_prefix` живёт в `m_text` терма и попадает в `JumpStmt::m_label`.

**AST:** break/continue/return/throw — класс `JumpStmt` с разными `Kind`
(`BreakStmt`/`ContinueStmt`/`ReturnStmt`/`ThrowStmt`), `m_label` (опционально), `m_value` (для return/throw).

## Оператор match (parser.y `match`)
`[значение] ==> { [шаблоны] --> тело; ...; [...] --> else; }`.
Раскладка MATCHING-терма: `m_left`=значение, `m_right`=match_body (BLOCK),
`m_right->m_block` = [item1, item2, ..., elseItem]. Каждый item: `m_left`=шаблоны
(matches: первый шаблон — сам терм, остальные — в его `m_block`), `m_right`=тело;
else: `m_left` = ELLIPSIS.
**AST:** класс `MatchStmt{MatchingStmt}`: `m_value`, `m_cases` (список `MatchCase{patterns, body}`), `m_default`.

## Codegen: break/continue, return, match и форматирование
- **break/continue.** Безымянные — обычные C++ `break;`/`continue;` (без goto). Именованные —
  `goto <имя>_break/_continue` (имя очищается от `::` через `cleanLabelName`). goto нужен только
  для именованных прерываний (передача управления между вложенными блоками/циклами).
- **Метки именованных блоков.** Именованный блок (`ScopeBlock` с именем) внутри функции выводит
  break-метку `<имя>_break:;` (после тела). continue-метку `<имя>_continue` ставит ПЕРВЫЙ цикл
  в теле блока (через `m_pendingContinueLabel`): именованный блок кладёт pending-метку, а первый
  while/do-while в его теле выводит её ПЕРЕД циклом (после инициализации блока), чтобы
  `goto <имя>_continue` просто переоценивал условие цикла, не повторяя инициализацию.
  Метки выводятся ТОЛЬКО внутри функций (`m_inFunction`), т.к. на namespace-scope C++-метки
  недопустимы; флаг и имя функции пробрасываются в `body_gen` через `emitBlockBodyToFile(..., inFunction=true)`.
- **Функция — top-level именованный блок.** Имя функции — блок для всей функции. Именованный
  break на имя текущей функции (`func:: ++`) = `return;` (void); именованный return
  (`func:: ++ value ++`) = `return value;` (определяется по `m_currentFuncName`).
- **return/throw.** `++ значение ++` = `func:: ++ значение ++` = `return значение;`; void-return
  `++ _ ++` = `func:: ++` (без `_` как зарезервированного значения) = `return;`; `throw <expr>;`.
- **match.** Значение вычисляется во временную переменную `auto _match<N> = <value>;` (на отдельной
  строке), затем ветки — через `if (_matchN == p1 || _matchN == p2) { body } ... else { default }`.
- **Нормальное форматирование.** Тела блоков (функций, if/while/do-while/match) генерируются
  многострочно с отступами (`m_indent`, 4 пробела на уровень): `{` в конце строки, каждый оператор
  на новой строке с отступом, `}` на своём уровне. Ветки последовательностей (ScopeBlock/sequence)
  внутри блоков также форматируются с отступом. На верхнем уровне (namespace-scope, indent==0)
  сохраняется прежнее поведение (зеркалирование строк исходника).




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
- `generateIfToFile()` / `generateWhileToFile()` / `generateDoWhileToFile()` — генерация control-flow
- `emitBlockBodyToFile()` / `emitBodyNode()` — генерация тела `{ ... }` с зеркалированием строк
- `resolveTypeName()` — C++-имя типа

**Маппинг control-flow:** каждый узел if/while/do-while маппится через `mapStart(node.range())/mapStop(node.range())`
(весь statement → сгенерированный C++). Тело каждой ветки маппится отдельно через диапазон блока `{ ... }`.
Исключение — do-while: begin statement'а совпадает с begin тела (начинается с `{`), что дало бы коллизию ключа
в `mapStop` (ключ = begin диапазона). Поэтому тело do-while не оборачивается собственным `mapStart/mapStop`
(`emitBodyNode(..., mapBlock=false)`) — всё покрывает единый range statement'а.

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
