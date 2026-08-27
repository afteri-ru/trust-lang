# MEMORY.md

> scope: include/lsp
> role: persistent-memory
> last_reviewed: 2026-08-26
> review_period: 30
> max_size: 7434

## Architecture

`trust-lsp` — LSP-сервер: `TrustLsp` — чистый диспетчер методов, маршрутизирует на stateless-сервисы
(`DocumentManager`, `AnalysisService`, `navigation`, `hover`, `completion`, `codeaction`, `formatting`).
In-process транспиляция Trust→C++ через `Context` (diag) + `runPipeline`. Транспорт —
`trust::transport::Transport` (stdin/stdout и TCP). Помимо LSP есть godbolt-стиль playground-режимы
`--json` (live-контракт `{source, cpp, trustToCpp, cppToTrust}`) и `--html`/`--html-full` (два редактора
Monaco) — модуль `lsp/html_emit.cpp`; статичные фрагменты HTML вынесены во внешние файлы и встраиваются
через `#embed`.

## Facts and invariants

- **Инвариант (TypeId резолвится только в реестре своего файла):** встроенные TypeId общие
  (иммутабельное ядро); пользовательские — пер-файловые (`registry_index = m_builtinCount + size + 1`),
  индексы могут численно совпадать у разных независимо транспилированных документов. Поэтому TypeId
  символа ВСЕГДА резолвится в реестре того `CachedSource`, которому принадлежит символ (символ и типы
  — из одной транспиляции; файл символа — по `fileIdx` из `nameRange`). TypeId никогда не передаётся в
  другой `CachedSource`. Импортированные типы уже лежат в реестре главного файла.
- **Единые источники имён:** имена пользовательского кода — таблица анализатора `SymbolIndex`
  (`collectSymbolItems`); встроенные типы/методы/функции/макросы — глобальный `BuiltinCatalog`
  (строится ОДИН раз на сервер), НЕ копируется в `CachedSource`. Доки макросов — единый глобальный
  `Context::macroDocs()` (ключ = первый терм без `@`), `BuiltinCatalog::macroDocs()` возвращает ССЫЛКУ
  без копии. Прагма-макросы НЕ входят в `m_predef_macro` (иначе сломался бы `PragmaCheck`); их доки
  сидируются прямо в ветках обработчиков, имена добавляются в `PredefMacroNames()`.
- **Опции анализа по источнику (окружение vs шебанг):** применяются ПО ИСТОЧНИКУ
  (`applyAnalysisArgsBySource`) в порядке приоритета `opts_.shebangMode`. Общие опции
  (`--solver-mode`, `--keywords`, `-fsolver-loop-unroll`, `-W...`) — ЦЕНТРАЛЬНО: определение —
  `commonAnalysisOptions`, применение — `applyAnalysisArgs`; LSP не объявляет их в своей таблице
  (собираются через analysis_passthrough). Ошибка опции ИЗ ШЕБАНГА публикуется обычной диагностикой
  `Severity::Error` на строке шебанга; ошибка ОКРУЖЕНИЯ — в лог; в `handleFormatting` (диагностики
  подавлены) и `html_emit` — в stderr. Нет «тихого» пропуска.
- **Ловушка (parseDriverArgs argv[0]):** `parseDriverArgs` ожидает полный argv (элемент [0] — имя
  программы). `applyAnalysisArgs` подставляет фиктивный argv[0], иначе первая опция списка (напр.
  `-Wsigil=ignore` из шебанга без `--run`) «съедалась» бы как имя программы.
- **Диагностики:** публикуются только ТЕКУЩЕГО trust-файла (фильтр по `trustReaderIdx`).
  `transpileSource` обёртывает `runPipeline` в try/catch — при исключении анализатора на частичном AST
  падение НЕ глотается (публикуется `internal analysis error`), символы дособираются из
  `Context::macroDefs()` через `appendMacroSymbols`.
- **Шебанг для `trust`** — комментарий лексера (опции через argv ОС); LSP открывает файл как текст,
  поэтому сам извлекает опции из шебанга, чтобы диагностики соответствовали реальному запуску.
