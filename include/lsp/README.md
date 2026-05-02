# lsp — Language Server Protocol

Autocomplete, go-to-definition, hover-диагностика, форматирование для IDE.

## Ответственность

- Autocomplete и подсказки при вводе
- Go-to-definition (переход к определению)
- Hover-диагностика (показ ошибок при наведении)
- Форматирование кода
- Error-коды (LSP Diagnostic.code)

## Компоненты

- **trust_lsp.h** — основной LSP-сервер (обработчики методов, кеширование source map)
- **lsp_protocol.h** — парсинг/сериализация JSON-RPC 2.0, Content-Length транспорт
- **transpile.h** — in-process транспиляция Trust → C++ + source map

## Зависимости

- `parser` — токенизация и парсинг
- `diag` — диагностика
- `types` — система типов
- `gencpp` — AST-структуры
