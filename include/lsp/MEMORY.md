# MEMORY.md

> scope: include/lsp
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 30000

# Архитектура trust-lsp (Language Server Protocol)

## Общая схема

```
VSCode Extension (extension.js)
    │
    └-- [LSP Client] -- stdin/stdout -- trust-lsp (C++ процесс)
            │
            ├-- TrustLsp - обработчик LSP-методов
            │     ├-- textDocument/didOpen        → in-process transpile (Trust → C++ + source map)
            │     ├-- textDocument/didChange       → re-transpile при изменении
            │     ├-- textDocument/didClose        → очищает reverse-кеш
            │     ├-- textDocument/definition     → trust_line → cpp_line (Go to Definition)
            │     ├-- textDocument/hover          → C++ код под курсором
            │     ├-- textDocument/inlayHint      → inline-подсказки "→ cpp:N"
            │     ├-- textDocument/documentLink   → кликабельные ссылки на C++ строки
            │     ├-- textDocument/completion     → автодополнение (имена, типы, макросы, члены)
            │     └-- workspace/didChangeConfiguration - обновление опций
            │
            ├-- LspProtocol (lsp_protocol.h/cpp) - набор свободных функций:
            │     readLspPacket, sendLspResponse, sendLspError,
            │     sendLspNotification, sendLspRequest
            │     Использует trust::transport::Transport (из utils/transport.hpp)
            │
            └-- Transpile (in-process, встраивается в TrustLsp::transpileSourceFile)
                  └-- transpileSourceFile() - Trust → C++ + source map (Context API)
```

## In-process транспиляция

### transpile() (lsp/transpile.h)

`transpile()` возвращает пару индексов (trustIdx, cppIdx), используя `Context` (из diag) для:

- регистрации входного файла (`add_source`)
- записи в выходной файл (`output_append`)
- построения маппинга trust↔cpp (`mapStart`/`mapStop`)
- создания Location для ошибок (`makeRange`/`makeLoc`)
- вывода ошибок (`report`)
- финализации (`toReader()` → `SourceMapReader`)

Работает без внешнего компилятора `trust`, строит source map in-memory. Результат хранится в `sourceCache_` (URI → пара индексов). Ошибки транспиляции не блокируют использование частичного кеша.

### Поддерживаемый синтаксис

Синтаксис: `create`, `print`, assignment, `if/then/else`, `while`, `macro define/expand`.

### Макросы

- Определение: `macro NAME <body>` на отдельной строке, с начала строки
- Вызов: `NAME;` - подставляет тело макроса
- Макро-маппинг: `addMacroMapping()` для навигации Go to Definition

## Сохранение транспилированного C++ на диск

При получении `textDocument/didOpen` trust-lsp транспилирует содержимое буфера (текст из didOpen) и кеширует source map. `textDocument/didChange` (синхронизация **Incremental**, `textDocumentSync: 2`) применяет правки к буферу в памяти и откладывает пере-транспиляцию через **debounce** (~200 мс), чтобы не транспилировать на каждый keystroke. Если в настройках (`LspOptions::tempDir`) указан каталог, сервер сохраняет сгенерированный C++ код на диск. Файл перезаписывается при каждой транспиляции.

Анализ идёт по тексту буфера (`openDocuments_`), а не по файлу на диске - так hover/documentLink/definition сразу отражают правки в редакторе даже до сохранения.

Настройка `tempDir` может быть изменена через LSP нотификацию `workspace/didChangeConfiguration`.

## Кэширование source-map

### Инвалидация кэша

`sourceCache_` хранит результат транспиляции для каждого `.src` файла:

| Событие | Поведение | Обоснование |
|---------|-----------|-------------|
| `didOpen` | Транспиляция текста буфера из didOpen, кеширование source map | Первое открытие - актуальный map сразу |
| `didChange` | Применение правок к буферу (Incremental) + отложенная пере-транспиляция через debounce; при запросе hover/definition/documentLink - синхронный flush | Не транспилировать на каждый keystroke |
| `didClose` | `sourceCache_` не очищается, очищается только reverse-кеш | Предотвращает потерю source-map при переключении вкладок |
| `shutdown` | Полная очистка кэша | Корректный re-initialize |

### Auto-recovery при cache-miss

Если при запросе `hover`, `definition` или `documentLink` кэш не найден, `getCachedReader()` автоматически выполняет транспиляцию на лету.

### Хеширование

Для проверки актуальности кэша используется `FileEntry::getHash()` (MD5 хеш содержимого файла).

## Новый API для LSP-методов

### SourceMapReader - расширенный API для LSP

Convenience-методы для LSP: `lspToLocation(idx, line, character)` (0-based → Location), `findRangeMap(loc)` (поиск полного RangeMap), `rangeToFragmentString(range)` (преобразование в URL-фрагмент), `getWordAt(loc)` (извлечение идентификатора под курсором), `getNameMappings()`.

`getCppName`/`getTrustName` возвращают полный `NameMap`: целевой диапазон - ВЕСЬ диапазон имени на противоположной стороне, без проекции/сдвига по позиции курсора внутри имени (иначе наведение на середину многосимвольного имени даёт сдвинутый target).

**Обратная навигация cppt → src в VSCode:** расширение регистрирует `.cppt`/`.hppt` с language id `cpp` (`package.json` → `contributes.languages`), иначе VSCode считает их plaintext и не отправляет hover/documentLink в LSP - обратный переход/подсветка из cppt не работают.

### Унификация ховеров и documentLink

`buildHoverContents()` - универсальный метод построения Markdown-массива ховера:

- Базовый блок с кодом противоположной стороны
- Выделение слова под курсором через `getWordAt`
- Для src файла:
  - обычное объявленное имя → `getCppName` (ссылка `→ C++` на C++-имя);
  - макрос → **две** ссылки: первичная `→ C++` на раскрытый код в `.cppt`
    (клик по тексту ведёт только в `.cppt`) и вторичная `Macro:` на определение
    макроса в сохранённом `dsl.src` (по `getMacroDefRange`; путь выводится из
    каталога `cppFilePath` → `<каталог cppt>/trust/dsl.src`);
- Для C++ файла: поиск `getTrustName` (ссылка `← Trust` на trust-определение);
  если NameMap не найден (expression-операторы, embed - у них нет NameMap) -
  fallback на statement-маппинг `findRangeMap` (backward cpp→trust) со ссылкой
  `← Trust: <text>` на trust-фрагмент

Полные правила и примеры - в `lsp/NAVIGATION.md`.

`handleDocumentLink()` - кликабельные ссылки по statement/NameMap. Для
trust→cpp цель макроса - раскрытый код в `.cppt` (клик ведёт только в `.cppt`).
Из результата удаляются диапазоны-надмножества (маппинг всей функции не должен
перекрывать точечные операторы). Подробности - в `lsp/NAVIGATION.md` §5.

### TrustLsp handler - единый каркас

Все хендлеры используют общий шаблон: конвертация LSP позиции → Location → поиск маппинга → чтение противоположной стороны.

### Трассировка (`--trace` / `trust.dev.traceLSP`)

При `LspOptions::trace` (флаг `--trace`, включается настройкой расширения `Trust: Trace LSP`) `TrustLsp::log()` пишет в stderr (попадает в канал «Trust Lang LSP») детальную диагностику:
- при `didOpen` - дамп всех маппингов source map (forward/backward/name) с координатами и текстом обеих сторон (`formatRange` → `path:line:col–line:col [текст]`);
- в `handleDocumentLink` (обе ветки) - каждая ссылка: исходный диапазон+текст → целевой диапазон+текст, с пометкой statement/name/macro;
- в `handleDefinition`/`handleHover` - курсор → найденный маппинг → цель;
- в `publishDiagnostics` - каждый diagnostic с диапазоном;
- в `getCachedReader` - reverse-поиск по `cppToTrustCache_` (hit/miss) и индексы reader-файлов.

## Разделение ответственности

| Компонент | Ответственность |
|-----------|----------------|
| **trust-lsp** (C++) | In-process транспиляция Trust→C++. Построение source map через Context. LSP-методы. Транспорт через stdin/stdout. |
| **VSCode extension** | Только запуск trust-lsp как LanguageClient. Никакого внешнего компилятора. |

## LSP-методы

| Метод | Описание |
|-------|----------|
| `initialize` | Принять capabilities, вернуть возможности сервера |
| `shutdown` / `exit` | Завершение работы |
| `textDocument/didOpen` | In-process транспиляция .src файла, кеширование source map |
| `textDocument/didChange` | Применить правки к буферу (Incremental) + отложенная пере-транспиляция (debounce) |
| `textDocument/didClose` | Очистить кеш source map |
| `textDocument/definition` | trust_line → {uri: .cppt, range: cpp_line} (F12). Поддерживает макро-маппинг |
| `textDocument/hover` | Показать Markdown-массив: C++/trust код + ссылки на определения |
| `textDocument/inlayHint` (LSP 3.17) | Показать "→ cpp:N" после каждой trust-строки |
| `textDocument/documentLink` | Сделать каждую trust-строку ссылкой на C++ |
| `textDocument/completion` | Автодополнение: имена, типы (`:`), макросы (`@`), члены (`obj.`) |
| `textDocument/codeAction` | Quickfix по fixits (WorkspaceEdit из FixitSuggestion диагностики) |

Server capabilities: `textDocumentSync: 2` (Incremental), `definitionProvider`, `hoverProvider`, `inlayHintProvider`, `documentLinkProvider`, `completionProvider` (`triggerCharacters: ["%","$",":","@","_","."]`), `codeActionProvider` (`codeActionKinds: ["quickfix"]`).

## Автодополнение (textDocument/completion)

Устойчиво к **недописанному/невалидному коду** (пока идёт набор): НЕ зависит от
успешной транспиляции и НЕ вызывает flush. Обработчик обёрнут в try/catch - сервер
никогда не падает на запросе.

ЕДИНЫЕ источники имён (без лексических проходов по тексту и пер-файловых копий реестра):

- **Имена пользовательского кода** (переменные/функции/типы) - таблица анализатора
  `SymbolIndex` (`collectSymbolItems`): сигнатура (`%`, `$`, `:`) сохраняется в label,
  фильтр видимости по позиции курсора (однопроходная семантика + скоуп). Реализация -
  модуль `lsp/completion`.
- **Встроенные типы/методы/функции/макросы** - глобальный `BuiltinCatalog`
  (`lsp/builtin_catalog`, строится ОДИН раз на сервер): общее иммутабельное ядро
  `TypeRegistry::builtinCore()` (типы+методы) + предопределённые макросы
  `Parser::PredefMacroNames()` + встроенные DSL-макросы. НЕ копируется в `CachedSource`.
- **Типы** (`:Имя`) - встроенные из каталога + пользовательские из пер-файлового реестра
  (`CachedSource.types`, `forEachType` userDefined).
- **Макросы** (`@...`) - predef/DSL из каталога + макроопределения, записанные анализатором
  (`SymbolIndex`, `isMacro=true`).
- **Члены** (`obj.`) - тип объекта по `SymbolInfo::type` (TypeId) → методы/поля из реестра
  (`TypeDescriptor::methods` + `TupleTypeData::elements`); поля словаря
  (`x := (a=1, b=2,)`) - из `SymbolInfo::dictFields` (инициализатор-литерал DictLiteral).
  Встроенный тип без реестра (нет кеша) - из каталога.

Вставка идёт через `textEdit` с диапазоном **набранного префикса** - сигнатура
(`@`, `%`, `:`, `$`) при вставке не дублируется. Поддерживаются Unicode/UTF-8 имена
(позиции LSP UTF-16 ↔ байтовые смещения UTF-8).

`vscode-languageclient` авто-регистрирует completion и codeAction provider по capability
сервера, поэтому правки в `extension.js` не требуются.

## Transport

Транспорт реализован через `trust::transport::Transport` (из `utils/transport.hpp`).
`LspProtocol` - набор свободных функций (readLspPacket, sendLspResponse, sendLspError, sendLspNotification, sendLspRequest), принимающих `trust::transport::Transport&`.
Поддерживается как stdin/stdout, так и TCP режим.

## Хранимые данные (CachedSource)

Структура `CachedSource`: `sourceMap` (unique_ptr<Context> после transpile), `cppOutput` (сгенерированный C++ код), `cppFilePath` (полный путь к .cppt), `trustReaderIdx`/`cppReaderIdx` (ReaderFile индексы в reader space), `symbols` (`SymbolIndex`) и `types` (`unique_ptr<TypeRegistry>`, пер-файловый; встроенные типы разделяются через общее ядро). Единый источник символов и типов - `SymbolIndex` + глобальный `BuiltinCatalog` (поля `typeSnapshot`/`symbolTypes` не используются).
Дополнительно: `cppToTrustCache_` (reverse-cache: cppFilePath → trustFilePath).

При `tempDir` (сохранение на диск) вместе с остальными заголовками в
`<tempDir>/trust/` сохраняется `dsl.src` (содержимое in-memory источника `@dsl`)
- чтобы ссылки «Macro:» на определения макросов были навигируемы. Отдельное поле
в `CachedSource` не требуется: путь к `dsl.src` выводится из `cppFilePath`
(`<каталог cppt>/trust/dsl.src`, см. `lsp/NAVIGATION.md` §2.2).

## Не трогает

- `src/debug/` - DAP не изменяется
- `src/parser/` - парсер не изменяется

## Playground-режимы (`--json` / `--html`)

Помимо LSP-режимов, `trust-lsp` умеет выводить результат in-process
транспиляции Trust→C++ для godbolt-стиля двухоконного playground:

- `--json [input]` - JSON-контракт `{source, cpp, trustToCpp, cppToTrust}` -
  «live-контракт», который страница/отдельный сервер запрашивает при каждом
  изменении кода.
- `--html [input]` - godbolt-стиль HTML-фрагмент: два редактора Monaco
  (Trust | C++ read-only) + встроенный glue-JS (Monarch-токенайзер Trust,
  инициализация, синхронная навигация по строкам, debounced живая
  пере-транспиляция через `--server-url`). `--html-full` - полная страница,
  `--monaco-url` - базовый путь сборки Monaco.

Реализация - `lsp/html_emit.cpp` (модуль `html_emit.h`): переиспользует
in-process пайплайн (Context + Pipeline + `toReader()`), проектирует forward/
backward маппинги на строки. Контракт и glue-JS API - в `lsp/HTML.md`.


## Данные анализатора в CachedSource

`CachedSource.symbols` (`SymbolIndex`): имя + `TypeId` + имя типа + диапазоны имени/скоупа,
а также `dictFields` (поля словаря из инициализатора-литерала) и `documentation`
(документирующий комментарий `///`/`##`/`/**`, т.ч. хвостовой `///<`/`##<`; для объявлений -
из `term->m_docs` грамматики через `AstNodeBase::documentation`, для макросов - из
`Context::macroDefs()` при записи в `recordMacro`). Источник - `PipelineResult::symbols` +
`Pipeline::releaseTypes()`.
Макроопределения записываются в `Context::macroDefs()` при парсинге и не теряются после
`PopScope` модуля. Автодополнение имён использует `collectSymbolItems` (тип в detail,
локальные фильтруются по позиции курсора, однопроходная семантика: имя на/после строки
курсора не предлагается); member-завершение - по `SymbolInfo::type` (TypeId) из
пер-файлового реестра `types`. `textDocument/hover` для имени под курсором, найденного в
`SymbolIndex`, выводит `documentation` (если непусто) в начало Markdown-содержимого ховера.

### Инвариант: TypeId резолвится только в реестре своего файла

Встроенные TypeId общие (иммутабельное ядро). Пользовательские типы - пер-файловые:
`registry_index = m_builtinCount + size + 1` (индексы могут численно совпадать у разных
независимо транспилированных документов). Поэтому **TypeId символа всегда резолвится в
реестре того `CachedSource`, которому принадлежит символ**: `CachedSource.symbols` и
`CachedSource.types` получены из одной транспиляции, а файл символа определяется по
`fileIdx` из `SymbolInfo::nameRange` (проверка принадлежности - через `ctx->source()`
этого `CachedSource`). TypeId никогда не передаётся в другой `CachedSource`. Импортированные
типы уже лежат в реестре главного файла - их TypeId валидны в нём. Это устраняет риск
cross-file коллизий числовых TypeId.

### Диагностика и fixup (publishDiagnostics / codeAction)

- `publishDiagnostics` публикует ВСЕ severity (Error/Fatal→1, Warning→2, Note→3, Remark→4),
  с `fixits` в кастомном поле (конвертация - общий `diag/protocol.hpp`:
  `severityToLsp`/`mapperRangeToProtocol`). Публикуются только диагностики ТЕКУЩЕГО trust-файла
  (фильтр по `trustReaderIdx`): диагностики импортированных модулей имеют собственные URI и не
  приписываются этому файлу.
- `textDocument/codeAction` строит CodeAction `kind=quickfix` с `WorkspaceEdit` из `fixits`
  переданной клиентом диагностики.
- `transpileSource` обёртывает `runPipeline` в try/catch: при исключении анализатора на
  частичном AST накопленные диагностики/символы/реестр сохраняются в `CachedSource`. Падение
  НЕ глотается молча - публикуется диагностика `internal analysis error: <what>` с диапазоном
  (начало файла), а символы дособираются из `Context::macroDefs()` через `appendMacroSymbols`
  (если падение произошло до семантического шага, например Fatal при парсинге).
