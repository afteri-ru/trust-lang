# MEMORY.md

> scope: include/pipeline/run
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Модель прогона: `RunRequest`/`PipelineResult` (`run/types.hpp`), фазы (`run/phases.hpp` — единая таблица
`PIPELINE_PHASE_LIST`, состояние `PhaseRun`), исполнение — `Pipeline::run`/`runPhases`, use-cases (`run.cpp`),
DSL-bootstrap (`dsl.cpp`).

## Facts and invariants

- **⚠ Целевой контракт — значение владельца, а не параметр фазы:** `target_options.hpp::targetInfoFromOptions`
  — единственная точка вычисления из `PipelineOpts`; результат хранит `TargetContract` (владелец —
  `Pipeline`). `BackendRequest::target` — невладеющая ссылка на контракт; стадии его не пересчитывают.

- **⚠ Фазы — единый источник порядка прогона:** `phases.hpp` (x-macro `PIPELINE_PHASE_LIST`) — единственный
  источник `PhaseKind`, имён и таблицы исполнения; `PhaseRun` — состояние (запрос, результат,
  HIR/MIR, гейты `semanticOk`/`stopped`). Оркестратор условий реализации не содержит; динамические условия —
  внутри фаз.
- **⚠ Один вектор корней:** семантика и lowering работают с `PipelineResult::astNodes` напрямую (отдельной
  копии вектора нет: она не добавляла семантики и расходилась бы при переписывании верхнего уровня).
  `PhaseRun::hir`/`mir` — единственные держатели HIR/MIR ДО публикации; `Finalize` переносит их в
  `PipelineResult` (второго одновременного хранилища нет).
- **⚠ Семантическая модель — продукт прогона, не опция:** `PipelineResult::semantics` — persistent-таблица
  символов и проекции (символы, moduleNames/aliases, importSites/reExports, using, namespaceAliases,
  references). Строится ВСЕГДА при шаге `Semantic` (в т.ч. на частичном AST).
- **⚠ `PipelineResult` наследует `backend::StageOutputs`:** артефакты codegen — из контракта бэкенда; `smt`
  принадлежит оркестратору (`--solver-mode`).
- **⚠ Два представления результата:** `astNodes` — AST после семантики (pre-lowering; мутации — только
  ErrorExpr-recovery и pre-semantic переписывание), `hirNodes` — HIR (вход C++-бэкенда и solver'а), `mir` — MIR
  (nullopt при нарушении гейта). Режимы
  (`behavioralModesFromOptions`) — ЕДИНСТВЕННАЯ точка разрешения флагов для кодогена и solver'а. HIR-фаза
  выполняется и на частичном AST (LSP recover), гейты верификаторов — только на успешной семантике.
- **⚠ Однофайловый режим `-fsingle-file`:** `int main` встраивается в тот же `.cppt` (нет
  `_main.cppt`/`SRC_MAIN`); по умолчанию при `--run`. «Модуль-скрипт» (top-level без `__main__`): операторы
  оборачиваются в синтезируемую entry, функции/типы/области/`{% %}` остаются на namespace. Entry может быть
  C++-embed, поэтому при `!sawEntry` проверяется по тексту `.cppt`.
- **Архив песочницы (`--emit-build-dir`) — однофайловый** (как `--run`).
- **Макросы:** `stdlib/dsl.src` грузится один раз в `Context`, каждый Parser наследует Macro через `Context&`;
  `--dsl <file>` заменяет, `--no-dsl` отключает. Заголовок пакетного `.src` маскируется до `ParseText`,
  чтобы метаданные не попадали в AST и строки сохранялись.
- **⚠ LSP-режим:** `allow_semantic_on_errors` — семантика на частичном AST; transpile при ошибках не
  выполняется. Владение `TypeSystem` разделяемое (`shared_ptr`): `TypeId` в `SemanticModel`/`SymbolIndex`
  валиден после Pipeline.

## Relations

- `semantic`, `ir`, `transpiler`, `solver`, `module_loader`, `session` (`Context`), `pipeline/backend`.
