# lsp — Language Server Protocol

Autocomplete, go-to-definition, hover-диагностика, форматирование для IDE.

## Ответственность

- Autocomplete и подсказки при вводе
- Go-to-definition (переход к определению)
- Hover-диагностика (показ ошибок при наведении)
- Форматирование кода
- Error-коды (LSP Diagnostic.code)

## Компоненты

- **error_codes.hpp** — Error-коды для LSP протокола (ErrorCode, ErrorCategory, ErrorInfo)
  - Категории: Lex, Parse, Type, Semantic, CodeGen, Internal
  - Маппинг кодов в строки (E001, E100...) и Severity

## Зависимости

- `parser` — токенизация и парсинг
- `diag` — диагностика
- `types` — система типов
- `gencpp` — AST-структуры
- `driver` — конвейер компиляции