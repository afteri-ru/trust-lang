# MEMORY.md

> scope: include/session
> role: persistent-memory
> last_reviewed: 2026-09-16
> review_period: 30
> max_size: 7000

## Architecture

Фасад `Context`: владеет `SourceMapWriter` (`sourcemap`), `DiagnosticEngine` + `Options` (`diag`),
`AttrPool` (`attrs`) и `Macro` (`syntax`, `shared_ptr`); невладеюще ссылается на `TypeRegistry` и
`ModuleLoader` (внедряются `Pipeline`). Хранит реестр макроопределений и доков, счётчики
макросов/гигиены/блоков и индекс текущего модуля.

## Facts and invariants

- `Context` non-copyable.
- **`setTypes`/`setLoader` — НЕВЛАДЕЮЩИЕ:** реестр типов и загрузчик модулей создаются и владеются
  вышележащим слоем (Pipeline) и внедряются указателем — это избегает циклов `diag↔types` и
  `diag↔module_loader`.
- `applyRegisteredDiagnostics` вызывается в конструкторе (базовая регистрация диагностик/флагов).
- `currentModule` — индекс текущего модуля = верх стека `ModuleLoader` (`nullopt` = не задан).
- `macroDocs` — СТАТИЧЕСКОЕ глобальное хранилище доков макросов (ключ = первый терм без `@`), общий
  источник для LSP-каталога; `macroDefs()` — записи текущего контекста (сохраняются после pop модуля).
- Счётчики `nextMacroCounter`/`nextHygienicCounter`/`nextBlockCounter` — per-Context, сбрасываются для тестов.
- `typeSets` (map имя→члены) — наборы типов текущего модуля (`Name ::= :A + :B;`), заполняются
  term→AST (`setTypeSets`) и используются разворотом функций (`findTypeSet`); перезаписывается на модуль.

## Decisions

- Фасад `Context` вынесен из `diag` в `session_lib`: чистые диагностики/опции не должны зависеть от
  хранения исходников и AST-атрибутов.

## Relations

- Зависит от `diag`, `sourcemap`, `attrs`; forward-decl — `types` (`TypeRegistry`), `module_loader`
  (`ModuleLoader`), `syntax` (`Macro`).
