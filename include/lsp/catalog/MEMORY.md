# MEMORY.md

> scope: include/lsp/catalog
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 5000

## Architecture

`BuiltinCatalog` (глобальный каталог встроенных имён), `TrustLsp` (диспетчер методов/транспорт),
`lsp_options` (опции по источнику), `lsp_utils`.

## Facts and invariants

- **Единые источники имён:** имена кода — `SymbolIndex`; встроенные — глобальный `BuiltinCatalog`
  (строится ОДИН раз, не копируется). Доки макросов — `Context::macroDocs()`; прагма-макросы НЕ входят в
  `m_predef_macro` (иначе ломается PragmaCheck).
- **⚠ Макросы: видимость per-module, не per-site:** `nullopt` = прелюдия/stdlib DSL (виден везде),
  значение = модуль определения. `isSymbolVisible` к макросам НЕ применяется.
- **⚠ Опции применяются ПО ИСТОЧНИКУ** (`applyAnalysisArgsBySource`, приоритет `opts_.shebangMode`):
  ошибка из шебанга — диагностика на строке шебанга, из окружения — в лог. Общие опции LSP не
  объявляет; ловушка `argv[0]` — как в `driver`.
- **Диагностики:** публикуются только ТЕКУЩЕГО trust-файла; исключение анализатора на частичном AST —
  `internal analysis error`, символы дособираются.
- **Шебанг для `trust`** — комментарий лексера (опции через argv ОС); LSP открывает файл как текст и
  извлекает опции сам.

## Relations

- `lsp/services`, `session`, `driver` (опции), `symbols`.
