# MEMORY.md

> scope: include/syntax (public API)
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 8000

## Architecture

Scanner (Flex) → Macro → Parser (Bison) → Term-дерево. Раскрытие макросов — НЕ отдельная стадия: лексер
подаёт термины в буфер, `Macro` раскрывает их до грамматики. Прагмы/предопределённые макросы —
`PredefMacroResolver`/`PragmaEvaluator`; алгоритмика макропроцессора — свободные функции, `Macro` — реестр
скоупов. **ЕДИНСТВЕННЫЕ источники списков макросов/прагм — x-macro** (`predef_macro_x.hpp`,
`pragma_macro_x.hpp`): реестры/доки/стрингификация строятся ОДИН раз. Реализация — в internal-подкаталогах:

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `syntax/parser/` | `parser/MEMORY.md` | грамматика, recovery, узлы Term для конструкций |
| `syntax/macro/` | `macro/MEMORY.md` | макропроцессор, DSL-скоуп, прагмы, контекст-макросы |
| `syntax/lexer/` | `lexer/MEMORY.md` | правила Flex (`lexer.l`), лексемы, литералы, EOF, правила идентификаторов (`NAMING.md`) |
| `syntax/grammar/` | `grammar/MEMORY.md` | вход генератора парсера (`parser.y.in`) и синтаксис конструкций (`SYNTAX.md`, `CREATE.md`) |
| `syntax/util/` | `util/MEMORY.md` | внутренние диагностики и warning-прагмы |
| `syntax/term/` | `term/MEMORY.md` | Term-дерево: `term.h` (`Term`, `dumpTermTree`), `term_types.h` (x-macro `TERMS` — единственный источник `TermID`) и реализация |

## Facts and invariants

- **⚠ MODULE-терм: импорт vs ссылка — `m_right`, а НЕ `isCall()`:** `m_right` означает ССЫЛКУ
  `\path::name` в любой позиции (индекс ставит `ResolveModuleReference` через `findLoaded` БЕЗ загрузки);
  импорт (`\path(...)`) `m_right` не имеет. Различать по `isCall()` нельзя: `\path::f(1)` — ссылка
  с `m_args`. Индексы загруженных модулей (`m_moduleIndices`) ставит загрузчик; пустой вектор — не сайт.
- **⚠ Заголовок файла: путь — не AST, блок интерфейса — AST.** Первая значимая конструкция `<path>;` или
  `<path> { ... };`: путь статичен (макроподстановка не применяется), маскируется перед разбором и в AST
  не входит; блок интерфейса разбирается как один `ScopeBlock`. Сканер 0-based, `Location::offset()`
  1-based. Заголовок обязателен для импортируемых/модульных/пакетных файлов, опционален только для
  корневой программы.
- **⚠ Имя entry-функции — ЕДИНСТВЕННАЯ формула `moduleEntryIdent(logical)`** (`location/module_name.hpp`):
  её строит DSL-макрос `main`, её же передаёт `Pipeline`; при расхождении обёртка `_main.cppt` не найдёт
  функцию.
- **⚠ API текста Term:** единственный read-аксессор `getText()` (string_view); запись — `setTextOwned`/
  `mutableText`. `Clone()` сохраняет природу текста. Term может владеть текстом только в syntax-фазе;
  AST при конвертации интернирует текст в пул строк компиляции ТОЛЬКО для узлов-носителей.
  Source-view Term валиден, пока жив SourceMap/Context.
- **⚠ `getText()` и `m_mapperRange` — независимые факты:** при раскрытии макроса spelling остаётся из
  определения, range переприсваивается на call-site. Не выводить одно из другого.

## Relations

- `ast` (TermToAstConverter), `module_loader` (индексы/интерфейс), `session` (`Context`/`Macro`),
  `utils` (trace_options), `location` (module_name).
