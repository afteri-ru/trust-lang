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
            ├── Transpile (in-process, lsp/transpile.h)
            │     └── transpileTrustSource() — Trust → C++ + source map без внешнего компилятора
            │
            └── TrustSource (из debug/trust_source.h)
                  └── in-memory source map для трансляции trust↔cpp строк
```

## Ключевые изменения (v2)

| Изменение | До | После |
|-----------|----|-------|
| Транспиляция | Внешний процесс `trust` через fork/exec | In-process через `transpileTrustSource()` |
| Передача `--compiler-path` | Требовалась в extension.js и LspOptions | Удалена полностью |
| Source map | Чтение из .map файла на диске | In-memory `TrustSource` после транспиляции |
| `configured_` флаг | Ожидание `workspace/didChangeConfiguration` | Удалён (ненужен без внешнего компилятора) |

## In-process транспиляция

### transpileTrustSource (lsp/transpile.h)

```
Trust код ──→ transpileTrustSource() ──→ CachedSource {
    .sourceMap = unique_ptr<TrustSource>,   // mapping trust→cpp
    .cppLines  = vector<string>             // сгенерированные C++ строки
}
```

- Работает без внешнего компилятора `trust`
- Строит source map in-memory: `TrustSource::addLineMapping()` / `addVarMapping()`
- Результат хранится в `sourceCache_` (URI → CachedSource)
- Ошибки транспиляции не блокируют использование частичного кеша

## Разделение ответственности

| Компонент | Ответственность |
|-----------|----------------|
| **trust-lsp** (C++) | In-process транспиляция Trust→C++. Построение source map. LSP-методы. Транспорт через stdin/stdout. |
| **VSCode extension** | Только запуск trust-lsp как LanguageClient. Никакого внешнего компилятора. |

## LSP-методы

### Стандартные

| Метод | Описание |
|-------|----------|
| `initialize` | Принять capabilities, вернуть возможности сервера |
| `shutdown` / `exit` | Завершение работы |
| `textDocument/didOpen` | In-process транспиляция .src файла, кеширование source map |
| `textDocument/didChange` | Re-transpile при изменении содержимого |
| `textDocument/didClose` | Очистить кеш source map |
| `textDocument/definition` | trust_line → {uri: .cpp, range: cpp_line} (F12) |
| `textDocument/hover` | Показать C++ код для trust-строки под курсором (из in-memory кеша) |
| `textDocument/inlayHint` (LSP 3.17) | Показать "→ cpp:N" после каждой trust-строки |
| `textDocument/documentLink` | Сделать каждую trust-строку ссылкой на C++ |

### Возможности (serverCapabilities)

```json
{
    "textDocumentSync": 1,
    "definitionProvider": true,
    "hoverProvider": true,
    "inlayHintProvider": true,
    "documentLinkProvider": { "resolveProvider": false }
}
```

## Transport

Реализован внутри `LspProtocol` (не отдельный класс транспорта):
- Чтение: `std::getline` + `Content-Length: N` → чтение N байт тела
- Запись: `Content-Length: <len>\r\n\r\n<body>`
- stdin/stdout транспорт (основной для VSCode)
- См. `src/lsp/lsp_protocol.cpp`

## Хранимые данные (CachedSource)

```cpp
struct CachedSource {
    unique_ptr<TrustSource> sourceMap;  // mapping trust→cpp
    vector<string>           cppLines;  // сгенерированные C++ строки
};
```

- `cppLines` используется в `textDocument/hover` для показа C++ кода без чтения файла с диска
- `sourceMap` используется для всех трансляций (definition, hover, inlayHint, documentLink)

## Не трогает

- `src/debug/` — DAP не изменяется
- `src/gencpp/` — transpiler не изменяется
- `src/parser/` — парсер не изменяется