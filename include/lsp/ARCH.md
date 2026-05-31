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
            │     ├── textDocument/didClose        → очищает кеш
            │     ├── textDocument/definition     → trust_line → cpp_line (Go to Definition)
            │     ├── textDocument/hover          → C++ код под курсором
            │     ├── textDocument/inlayHint      → inline-подсказки "→ cpp:N"
            │     └── textDocument/documentLink   → кликабельные ссылки на C++ строки
            │
            ├── LspProtocol — парсинг/сериализация LSP (JSON-RPC 2.0)
            │     ├── Content-Length парсинг
            │     ├── JSON-RPC 2.0 диспетчеризация (request/response/notification)
            │     └── stdin/stdout транспорт
            │
            └── Transpile (in-process, lsp/transpile.h)
                  └── transpile() — Trust → C++ + source map (Context API)
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

При получении `textDocument/didOpen` или `textDocument/didChange` trust-lsp выполняет in-process транспиляцию и кеширует source map. Если в настройках (`LspOptions::tempDir`) указан каталог, сервер сохраняет сгенерированный C++ код на диск. Файл перезаписывается при каждой транспиляции.

Настройка `tempDir` может быть изменена через LSP нотификацию `workspace/didChangeConfiguration`.

## Кэширование source-map

### Инвалидация кэша

`sourceCache_` хранит результат транспиляции для каждого `.src` файла:

| Событие | Поведение | Обоснование |
|---------|-----------|-------------|
| `didOpen` | Проверка хеша: если кэш есть и хеш содержимого совпадает — транспиляция не выполняется | Предотвращает повторную транспиляцию при переключении вкладок |
| `didChange` | Безусловная перетранспиляция | Содержимое изменилось |
| `didClose` | `sourceCache_` не очищается, очищается только reverse-кеш | Предотвращает потерю source-map при переключении вкладок |
| `shutdown` | Полная очистка кэша | Корректный re-initialize |

### Auto-recovery при cache-miss

Если при запросе `hover`, `definition` или `documentLink` кэш не найден, `getCachedReader()` автоматически выполняет транспиляцию на лету.

### Хеширование

Для проверки актуальности кэша используется `FileEntry::getHash()` (MD5 хеш содержимого файла).

## Новый API для LSP-методов

### SourceMapReader — расширенный API для LSP

Convenience-методы для LSP: `lspToLocation(idx, line, character)` (0-based → Location), `findRangeMap(loc)` (поиск полного RangeMap), `rangeToFragmentString(range)` (преобразование в URL-фрагмент), `getWordAt(loc)` (извлечение идентификатора под курсором), `getNameMappings()`.

### Унификация ховеров и documentLink

`buildHoverContents()` — универсальный метод построения Markdown-массива ховера:

- Базовый блок с кодом противоположной стороны
- Выделение слова под курсором через `getWordAt`
- Для src файла: поиск `getCppName` (ссылка на C++ определение) и `getMacroDefRange` (ссылка на определение макроса)
- Для C++ файла: поиск `getTrustName` (ссылка на trust-определение)

`handleDocumentLink()` — NameMap-ссылки для переменных.

### TrustLsp handler — единый каркас

Все хендлеры используют общий шаблон: конвертация LSP позиции → Location → поиск маппинга → чтение противоположной стороны.

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
| `textDocument/didChange` | Re-transpile при изменении содержимого |
| `textDocument/didClose` | Очистить кеш source map |
| `textDocument/definition` | trust_line → {uri: .cppt, range: cpp_line} (F12). Поддерживает макро-маппинг |
| `textDocument/hover` | Показать Markdown-массив: C++/trust код + ссылки на определения |
| `textDocument/inlayHint` (LSP 3.17) | Показать "→ cpp:N" после каждой trust-строки |
| `textDocument/documentLink` | Сделать каждую trust-строку ссылкой на C++ |

Server capabilities: `textDocumentSync: 1`, `definitionProvider`, `hoverProvider`, `inlayHintProvider`, `documentLinkProvider`.

## Transport

Реализован внутри `LspProtocol` (не отдельный класс транспорта): Content-Length парсинг, stdin/stdout транспорт.

## Хранимые данные (CachedSource)

Структура `CachedSource`: `sourceMap` (Context после transpile), `cppOutput` (сгенерированный C++ код), `cppFilePath` (полный путь к .cppt), `trustReaderIdx`/`cppReaderIdx` (индексы в reader space).

## Не трогает

- `src/debug/` — DAP не изменяется
- `src/parser/` — парсер не изменяется
