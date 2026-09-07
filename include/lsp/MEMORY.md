# MEMORY.md

> scope: include/lsp
> role: persistent-memory
> last_reviewed: 2026-08-26
> review_period: 30
> max_size: 8921

## Architecture

`trust-lsp` — LSP-сервер: `TrustLsp` — чистый диспетчер методов, маршрутизирует на stateless-сервисы
(DocumentManager, AnalysisService, navigation, hover, completion, codeaction, formatting). In-process
транспиляция Trust→C++ через `Context` + `runPipeline`. Транспорт — stdin/stdout и TCP. Помимо LSP есть
playground-режимы `--json` (live-контракт `{source, cpp, trustToCpp, cppToTrust}`) и `--html`/`--html-full`
(модуль `lsp/html_emit.cpp`); статичные HTML-фрагменты встраиваются через `#embed`.

## Facts and invariants

- **TypeId резолвится ТОЛЬКО в реестре своего файла:** встроенные — общие (иммутабельное ядро);
  пользовательские — пер-файловые (`registry_index = m_builtinCount + size + 1`), индексы могут численно
  совпадать у разных независимо транспилированных документов. TypeId символа — в реестре того
  `CachedSource`, которому принадлежит символ; в другой `CachedSource` не передаётся.
- **Единые источники имён:** имена кода — `SymbolIndex`; встроенные типы/методы/функции/макросы —
  глобальный `BuiltinCatalog` (строится ОДИН раз на сервер), НЕ копируется в `CachedSource`. Доки
  макросов — единый `Context::macroDocs()` (ключ = первый терм без `@`), возврат по ссылке без копии.
  Прагма-макросы НЕ входят в `m_predef_macro` (иначе сломался бы PragmaCheck).
- **Опции по источнику (окружение vs шебанг):** применяются ПО ИСТОЧНИКУ (`applyAnalysisArgsBySource`),
  приоритет `opts_.shebangMode`. Общие опции (`--solver-mode`, `--keywords`, `-fsolver-loop-unroll`, `-W`)
  — ЦЕНТРАЛЬНО (`commonAnalysisOptions`/`applyAnalysisArgs`); LSP их не объявляет (analysis_passthrough).
  Ошибка опции ИЗ ШЕБАНГА — обычная диагностика на строке шебанга; из ОКРУЖЕНИЯ — в лог;
  formatting/html_emit — в stderr. Нет «тихого» пропуска.
- **Ловушка (argv[0]):** `parseDriverArgs` ожидает полный argv (элемент [0] — имя программы);
  `applyAnalysisArgs` подставляет фиктивный argv[0], иначе первая опция списка «съедалась» бы как имя программы.
- **Диагностики:** публикуются только ТЕКУЩЕГО trust-файла (фильтр по `trustReaderIdx`). `transpileSource`
  обёртывает `runPipeline` в try/catch — исключение анализатора на частичном AST публикуется как
  `internal analysis error`, символы дособираются через `appendMacroSymbols`.
- **Шебанг для `trust`** — комментарий лексера (опции через argv ОС); LSP открывает файл как текст,
  поэтому сам извлекает опции из шебанга, чтобы диагностики соответствовали реальному запуску.
