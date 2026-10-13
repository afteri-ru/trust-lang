# MEMORY.md

> scope: include/session
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 4000

## Architecture

Фасад `Context`: владеет `SourceMapWriter`, `DiagnosticEngine` + `Options`, `AttrPool` и `Macro`
(`shared_ptr`); невладеюще ссылается на `TypeSystem` и `ModuleResolver`; реализует
`diag::SourceTextProvider` (текст исходной строки для печати диагностик). Детали — `session/README.md`.

## Facts and invariants

- **⚠ `ModuleResolver` — низкий слой для парсера:** `include/session/module_resolver.hpp`
  (`resolveModules`, `findLoaded` — индекс УЖЕ загруженного модуля БЕЗ загрузки/парсинга,
  `moduleLogicalName`, `registerModuleAlias`/`hasModuleAlias`, `currentModuleIndex`); реализация —
  `ModuleLoader`. Доступ к API загрузчика через `Context` НЕ предоставляется.
- **⚠ Владение — Pipeline:** загрузчик и реестр типов внедряются НЕВЛАДЕЮЩИМИ указателями (иначе
  циклы `diag↔types` и `diag↔module_loader`); владелец обоих — `Pipeline`.
- **⚠ `strings()` — пул строк КОМПИЛЯЦИИ (per-Context `StringInterner`, `shared_ptr`),** разделяется с
  `PipelineResult`; `StringInternerScope` делает его активным на время `Pipeline::run`/`termToAst`.
  Это владелец всех текстов AST/HIR/SymbolIndex; `manualStringInterner()` — только вне компиляции.
- **⚠ `macroDocs` — СТАТИЧЕСКОЕ процессное хранилище доков макросов** (ключ = первый терм без `@`),
  общий источник для LSP-каталога: значения — view в ПРОЦЕССНЫЙ пул (`manualStringInterner()`), т.к.
  хранилище живёт весь процесс (per-Context пул дал бы dangling). `MacroDef::module` — модуль разбора
  (`nullopt` = predef/stdlib DSL); макросы НЕ экспортируются, поле нужно только для видимости в LSP.
- Счётчики `nextMacroCounter`/`nextHygienicCounter`/`nextBlockCounter` — per-Context, сбрасываются для
  тестов; `applyRegisteredDiagnostics` вызывается конструктором; `Context` non-copyable.

## Decisions

- Фасад `Context` вынесен из `diag` в `session_lib`: чистые диагностики/опции не должны зависеть от
  хранения исходников и AST-атрибутов.

## Relations

- Зависит от `diag`, `sourcemap`, `attrs`; forward-decl — `types`, `module_loader`, `syntax` (`Macro`).
