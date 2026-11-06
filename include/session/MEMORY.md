# MEMORY.md

> scope: include/session
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

Фасад `Context`: владеет `SourceMapWriter`, `DiagnosticEngine` + `Options`, `AttrPool` и `Macro`
(`shared_ptr`); невладеюще ссылается на `TypeSystem` и `ModuleResolver`; реализует
`diag::SourceTextProvider` (текст исходной строки для печати диагностик). Детали — `session/README.md`.

## Facts and invariants

- **⚠ `ModuleResolver` — низкий слой для парсера:** `include/module_api/module_resolver.hpp`
  (`resolveModules`, `findLoaded` — индекс УЖЕ загруженного модуля БЕЗ загрузки/парсинга,
  `moduleLogicalName`, `currentModuleIndex`); реализация —
  `ModuleLoader`. Доступ к API загрузчика через `Context` НЕ предоставляется.
- **⚠ Владение — Pipeline:** загрузчик и реестр типов внедряются НЕВЛАДЕЮЩИМИ указателями (иначе
  циклы `diag↔types` и `diag↔module_loader`); владелец обоих — `Pipeline`.
- **⚠ `strings()` — пул строк КОМПИЛЯЦИИ (per-Context `StringInterner`, `shared_ptr`),** разделяется с
  `PipelineResult`; `StringInternerScope` делает его активным на время жизни `Pipeline` (включая ctor) и
  `Parser`, а также прямого `termToAst`. Это владелец всех текстов AST/HIR/SymbolIndex;
  `manualStringInterner()` — только вне компиляции по явному `ManualInternerScope`.
- **⚠ Доки макросов принадлежат ПРОГОНУ:** `Context::macroDocs()/macroDoc()` — хранилище прогона
  (predef/DSL-сидинг + `##`-доки определений + прагма-переопределения); значения — view в пул строк
  прогона (`strings()`), живут, пока жив Context/результат. LSP читает доки у пер-файлового Context.
  `MacroDef::module` — модуль разбора (`nullopt` = predef/stdlib DSL); макросы НЕ экспортируются, поле
  нужно только для видимости в LSP.
- Счётчики `nextMacroCounter`/`nextHygienicCounter`/`nextBlockCounter` — per-Context, сбрасываются для
  тестов; `applyRegisteredDiagnostics` вызывается конструктором; `Context` non-copyable.
- **⚠ `Context::runServices()` — ЕДИНЫЙ read-model порт прогона (`RunServices`, `session/run_services.hpp`):**
  снимок невладеющих ссылок (реестр `readTypes()`, `diag`/`opts`/`source`/`attrs`/`declCppFacts`/`target`/`moduleResolver`).
  Мутабельного `types` порт не даёт (R12/E4). Потребители — кодоген (`CodegenServices` = псевдоним), solver, стадии
  бэкенда; строится от полного `Context` (требует инжекта `TypeSystem`/`TargetContract`).
- **⚠ `Context::expansionChain(range)` — реализация порта `diag::SourceTextProvider`:** делегирует
  `SourceMapWriter::macroExpansionChain` (владелец пары `use → definition`); отдельного хранилища цепочки
  нет, имя раскрытия — слово текстового буфера. `diag` остаётся листом (R34).
- **⚠ `Context::types()` — ТОЛЬКО `const` (read-model, R12/E1):** mutable-реестр фасад НЕ отдаёт. Зона записи
  (семантика/lowering) получает `TypeSystem&` явно от владельца `Pipeline` (`AnalysisContext::registry`,
  `TypeResolution::registry`, `LowerCtx::types`, `SemanticPassRunner(ctx, registry)`); `buildHir` принимает реестр параметром.

## Decisions

- Фасад `Context` вынесен из `diag` в `session_lib`: чистые диагностики/опции не должны зависеть от
  хранения исходников и AST-атрибутов.

## Relations

- Зависит от `diag`, `sourcemap`, `attrs`, `module_api` (`ModuleResolver`); forward-decl — `types`,
  `module_loader`, `syntax` (`Macro`).
- **`ast`/`syntax` НЕ зависят от `session` (R34):** получают порты `ConvertServices`/`ParserServices`
  (диагностика/источник/атрибуты/опции/интернер), собираемые владельцем на точке сборки; ребро запрещено гейтом.
