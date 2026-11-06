# MEMORY.md

> scope: include/lsp/services
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Stateless-сервисы LSP: `analysis_service` (публикация диагностик/символов), `completion`, `hover_service`,
`navigation_service`, `code_action`, `formatting_service`, `document_manager` (кэш `CachedSource`).

## Facts and invariants

- **⚠ Символы — из `SemanticModel::symbols()`:** имя ХРАНИТСЯ С сигналом (`$`/`%`/`@`/`:`),
  `namespacePath` — без сигналов; сопоставление под курсором — `completion::stripSigil`.
  Индекс собирается ОДИН раз в фазе семантики (хук не отключается).
- **⚠ `CachedSource` держит `std::optional<SemanticModel>`:** сайты импорта, реэкспорт фасадов,
  `using`-скоупы, namespace-алиасы и ссылки читаются из модели (копий нет).
  Единственный парсер масок — загрузчик; сайты уже отфильтрованы по `importer == mainModuleIndex`
  (кроме позиционного фильтра `lineInScope` в completion).
- **⚠ Завершение не показывает невидимые имена (per-site):** фильтр — `isSymbolVisible`; позиционный
  фильтр — ТОЛЬКО для объявлений текущего файла. Private/import-hidden типы — только для собственного
  модуля; квалифицированные имена фильтруются по `namespacePath`.
- **⚠ Переход/hover по ССЫЛКЕ — из `SemanticModel::references()`:** диапазон ссылки расширяется до
  конца qualified-токена; относительное имя разрешается от `projectDir`, а не от CWD.
- **⚠ Тексты символов — view в пул строк компиляции:** `SemanticModel` держит `shared_ptr`
  пула (`Context::stringsPool()`), `CachedSource` держит `Context`; `name`/`documentation`
  валидны между запросами.
- **⚠ TypeId резолвится ТОЛЬКО в реестре своего файла:** индексы разных документов могут совпадать.

- **Диагностики:** `publishDiagnostics` публикует только ТЕКУЩЕГО trust-файла; range резолвится
  ЕДИНЫМ декодером `diag::diagnosticLocation` (severity — единый `severityToLsp`; LSP-специфика —
  0-based `{line-1, column-1}`); повторной конверсии offset→локация нет.

## Relations

- `lsp/catalog`, `symbols` (`SymbolIndex`), `pipeline` (проекции), `session` (`Context`).
