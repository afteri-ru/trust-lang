# MEMORY.md

> scope: include/syntax (public API)
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

Scanner (Flex) → Macro → Parser (Bison) → Term-дерево. Раскрытие макросов — НЕ отдельная стадия: лексер
подаёт термины в буфер, `Macro` раскрывает их до грамматики. Прагмы/предопределённые макросы —
`PredefMacroResolver`/`PragmaEvaluator`; алгоритмика макропроцессора — свободные функции, `Macro` — реестр
скоупов. **ЕДИНСТВЕННЫЕ источники списков макросов/прагм — x-macro** (`predef_macro_x.hpp`,
`pragma_macro_x.hpp`).

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `syntax/parser/` | есть | грамматика, recovery, узлы Term |
| `syntax/macro/` | есть | макропроцессор, DSL-скоуп, прагмы, контекст-макросы |
| `syntax/lexer/` | есть | правила Flex (`lexer.l`), лексемы, литералы, EOF, правила идентификаторов |
| `syntax/grammar/` | есть | вход генератора парсера (`parser.y.in`), синтаксис (`SYNTAX.md`, `CREATE.md`) |
| `syntax/util/` | есть | внутренние диагностики и warning-прагмы |
| `syntax/term/` | есть | Term-дерево: `term.h`, `term_types.h` (x-macro `TERMS` — единственный источник `TermID`) |

## Facts and invariants

- **⚠ Парсер не зависит от `session` (R34):** `Parser`/`Macro`/`Scanner`/`PredefMacroResolver`/`PragmaEvaluator`
  принимают порт `ParserServices{diag, source, opts, strings, moduleResolver, macro, currentParseModule}`
  (`syntax/parser_services.hpp`), собираемый владельцем (`makeParserServices`). `Parser` делает
  `strings` АКТИВНЫМ пулом на время своего существования — доки/тексты разбора принадлежат прогону (E9).
  Доки макросов пишутся через sink-и (`addMacroDoc`/`setMacroDoc`), регистрация — `recordMacro`-sink;
  `value`/`context`-доки сидируются в ctor `PredefMacroResolver`, прагм-доки — при использовании прагмы.
  Ребро `syntax→session` запрещено гейтом. `Macro` хранит порт ПО ЗНАЧЕНИЮ (живёт дольше разбора).
- **⚠ MODULE-терм — только сайт импорта:** `\path(...)` (`CheckModuleTerm`); индексы загруженных
  модулей ставит загрузчик (`m_moduleIndices`; пустой вектор — не сайт). Ссылочная форма
  `\path::name` удалена из грамматики (`m_right` не производится).
- **⚠ Заголовок файла: путь — не AST, блок интерфейса — AST.** `<path>;` или `<path> { ... };`: путь статичен
  (макроподстановки нет), маскируется до разбора и в AST не входит; блок — один `ScopeBlock`. Сканер 0-based,
  `Location::offset()` 1-based. Заголовок обязателен для импортируемых/модульных/пакетных файлов, опционален —
  только для корневой программы.
- **⚠ Имя entry-функции — ЕДИНСТВЕННАЯ формула `moduleEntryIdent(logical)`** (`location/module_name.hpp`): её
  строит DSL-макрос `main`, её же передаёт `Pipeline`; при расхождении `_main.cppt` не найдёт функцию.
- **⚠ API текста Term:** read — `getText()` (string_view); запись — `setTextOwned`/`mutableText`. `Clone()`
  сохраняет природу текста. Term владеет текстом только в syntax-фазе; AST интернирует текст в пул строк
  ТОЛЬКО для узлов-носителей. Source-view Term валиден, пока жив SourceMap/Context.
- **⚠ `getText()` и `m_mapperRange` — независимые факты:** при раскрытии макроса spelling — из определения,
  range переприсваивается на call-site. Не выводить одно из другого.

## Relations

- `ast` (TermToAstConverter), `module_loader` (индексы/интерфейс), `utils` (trace_options),
  `location` (module_name). Порт `ParserServices` (диагностика/источник/опции/резолвер) собирает владелец —
  `syntax` от `session` не зависит.
