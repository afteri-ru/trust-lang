# Архитектура trust-lsp (Language Server Protocol)

## Общая схема

```
VSCode Extension (extension.js)
    │
    └── [LSP Client] ── stdin/stdout ── trust-lsp (C++ процесс)
            │
            ├── TrustLsp — обработчик LSP-методов
            │     ├── textDocument/didOpen        → in-process transpile (Trust → C++ + source map)
            │     ├── textDocument/didChange       → re-transpile при изменении
            │     ├── textDocument/didClose        → очищает reverse-кеш
            │     ├── textDocument/definition     → trust_line → cpp_line (Go to Definition)
            │     ├── textDocument/hover          → C++ код под курсором
            │     ├── textDocument/inlayHint      → inline-подсказки "→ cpp:N"
            │     ├── textDocument/documentLink   → кликабельные ссылки на C++ строки
            │     └── workspace/didChangeConfiguration — обновление опций
            │
            ├── LspProtocol (lsp_protocol.h/cpp) — набор свободных функций:
            │     readLspPacket, sendLspResponse, sendLspError,
            │     sendLspNotification, sendLspRequest
            │     Использует trust::transport::Transport (из utils/transport.hpp)
            │
            └── Transpile (in-process, встраивается в TrustLsp::transpileSourceFile)
                  └── transpileSourceFile() — Trust → C++ + source map (Context API)
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
- Вызов: `NAME;` — подставляет тело макроса
- Макро-маппинг: `addMacroMapping()` для навигации Go to Definition

## Сохранение транспилированного C++ на диск

При получении `textDocument/didOpen` trust-lsp транспилирует содержимое буфера (текст из didOpen) и кеширует source map. `textDocument/didChange` (синхронизация **Incremental**, `textDocumentSync: 2`) применяет правки к буферу в памяти и откладывает пере-транспиляцию через **debounce** (~200 мс), чтобы не транспилировать на каждый keystroke. Если в настройках (`LspOptions::tempDir`) указан каталог, сервер сохраняет сгенерированный C++ код на диск. Файл перезаписывается при каждой транспиляции.

Анализ идёт по тексту буфера (`openDocuments_`), а не по файлу на диске — так hover/documentLink/definition сразу отражают правки в редакторе даже до сохранения.

Настройка `tempDir` может быть изменена через LSP нотификацию `workspace/didChangeConfiguration`.

## Кэширование source-map

### Инвалидация кэша

`sourceCache_` хранит результат транспиляции для каждого `.src` файла:

| Событие | Поведение | Обоснование |
|---------|-----------|-------------|
| `didOpen` | Транспиляция текста буфера из didOpen, кеширование source map | Первое открытие — актуальный map сразу |
| `didChange` | Применение правок к буферу (Incremental) + отложенная пере-транспиляция через debounce; при запросе hover/definition/documentLink — синхронный flush | Не транспилировать на каждый keystroke |
| `didClose` | `sourceCache_` не очищается, очищается только reverse-кеш | Предотвращает потерю source-map при переключении вкладок |
| `shutdown` | Полная очистка кэша | Корректный re-initialize |

### Auto-recovery при cache-miss

Если при запросе `hover`, `definition` или `documentLink` кэш не найден, `getCachedReader()` автоматически выполняет транспиляцию на лету.

### Хеширование

Для проверки актуальности кэша используется `FileEntry::getHash()` (MD5 хеш содержимого файла).

## Новый API для LSP-методов

### SourceMapReader — расширенный API для LSP

Convenience-методы для LSP: `lspToLocation(idx, line, character)` (0-based → Location), `findRangeMap(loc)` (поиск полного RangeMap), `rangeToFragmentString(range)` (преобразование в URL-фрагмент), `getWordAt(loc)` (извлечение идентификатора под курсором), `getNameMappings()`.

`getCppName`/`getTrustName` возвращают полный `NameMap`: целевой диапазон — ВЕСЬ диапазон имени на противоположной стороне, без проекции/сдвига по позиции курсора внутри имени (иначе наведение на середину многосимвольного имени даёт сдвинутый target).

**Обратная навигация cppt → src в VSCode:** расширение регистрирует `.cppt`/`.hppt` с language id `cpp` (`package.json` → `contributes.languages`), иначе VSCode считает их plaintext и не отправляет hover/documentLink в LSP — обратный переход/подсветка из cppt не работают.

### Унификация ховеров и documentLink

`buildHoverContents()` — универсальный метод построения Markdown-массива ховера:

- Базовый блок с кодом противоположной стороны
- Выделение слова под курсором через `getWordAt`
- Для src файла: поиск `getCppName` (ссылка на C++ определение) и `getMacroDefRange` (ссылка на определение макроса)
- Для C++ файла: поиск `getTrustName` (ссылка на trust-определение); если NameMap не найден (expression-операторы, embed — у них нет NameMap) — fallback на statement-маппинг `findRangeMap` (backward cpp→trust) со ссылкой `← Trust: <text>` на trust-фрагмент

`handleDocumentLink()` — NameMap-ссылки для переменных.

### TrustLsp handler — единый каркас

Все хендлеры используют общий шаблон: конвертация LSP позиции → Location → поиск маппинга → чтение противоположной стороны.

### Трассировка (`--trace` / `trust.traceLSP`)

При `LspOptions::trace` (флаг `--trace`, включается настройкой расширения `Trust: Trace LSP`) `TrustLsp::log()` пишет в stderr (попадает в канал «Trust Lang LSP») детальную диагностику:
- при `didOpen` — дамп всех маппингов source map (forward/backward/name) с координатами и текстом обеих сторон (`formatRange` → `path:line:col–line:col [текст]`);
- в `handleDocumentLink` (обе ветки) — каждая ссылка: исходный диапазон+текст → целевой диапазон+текст, с пометкой statement/name/macro;
- в `handleDefinition`/`handleHover` — курсор → найденный маппинг → цель;
- в `publishDiagnostics` — каждый diagnostic с диапазоном;
- в `getCachedReader` — reverse-поиск по `cppToTrustCache_` (hit/miss) и индексы reader-файлов.

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

Server capabilities: `textDocumentSync: 2` (Incremental), `definitionProvider`, `hoverProvider`, `inlayHintProvider`, `documentLinkProvider`.

## Transport

Транспорт реализован через `trust::transport::Transport` (из `utils/transport.hpp`).
`LspProtocol` — набор свободных функций (readLspPacket, sendLspResponse, sendLspError, sendLspNotification, sendLspRequest), принимающих `trust::transport::Transport&`.
Поддерживается как stdin/stdout, так и TCP режим.

## Хранимые данные (CachedSource)

Структура `CachedSource`: `sourceMap` (unique_ptr<Context> после transpile), `cppOutput` (сгенерированный C++ код), `cppFilePath` (полный путь к .cppt), `trustReaderIdx`/`cppReaderIdx` (ReaderFile индексы в reader space).
Дополнительно: `cppToTrustCache_` (reverse-cache: cppFilePath → trustFilePath).

## Не трогает

- `src/debug/` — DAP не изменяется
- `src/parser/` — парсер не изменяется
