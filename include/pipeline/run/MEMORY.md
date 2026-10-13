# MEMORY.md

> scope: include/pipeline/run
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 8000

## Architecture

Модель прогона: `RunRequest`/`PipelineResult` (`run/types.hpp`), исполнение — `Pipeline::run`
(`pipeline.cpp`), use-cases (`run.cpp`), DSL-bootstrap (`dsl.cpp`).

## Facts and invariants

- **⚠ Семантическая модель — продукт прогона, не опция:** `PipelineResult::semantics`
  (`std::optional<SemanticModel>`) содержит persistent-таблицу символов и проекции (символы,
  `moduleNames`/`moduleAliases`, `importSites`/`reExports` главного модуля, `usingScopes`,
  `namespaceAliases`, `exportedNamespaceAliases`, `references`). Строится ВСЕГДА при шаге
  `Semantic` (в т.ч. на частичном AST) — флага сбора нет.
- **⚠ `PipelineResult` наследует `backend::StageOutputs`:** артефакты codegen (`cpp`/`header`) — из
  контракта бэкенда; `smt` (SMT-скрипт) принадлежит оркестратору (`--solver-mode` печатает/исполняет его).
- **⚠ Два представления результата:** `astNodes` — НЕизменённая AST (pre-lowering; её читают
  диагностики/LSP), `hirNodes` — HIR (вход C++-бэкенда и solver'а), `mir` — MIR (`nullopt`, если гейт
  `verifyHir`/`verifyFlowModule` нарушен). Режимы (`behavioralModesFromOptions`) — ЕДИНСТВЕННАЯ точка
  разрешения флагов, передаётся и в кодоген, и в solver. `SemanticPassRunner` HIR не строит; HIR-фаза
  выполняется и на частичном AST (LSP recover), гейты верификаторов — только на успешной семантике.
- **⚠ Однофайловый режим `-fsingle-file`:** обёртка `int main` встраивается в тот же `.cppt` модуля (нет
  `_main.cppt`/`SRC_MAIN`); по умолчанию при `--run`. «Модуль-скрипт» (top-level код без `__main__`):
  операторы оборачиваются в синтезируемую entry, функции/типы/области/`{% %}`-embed остаются на
  namespace. Entry может задаваться C++-embed, поэтому при `!sawEntry` он проверяется по тексту `.cppt`.
- **Архив песочницы (`--emit-build-dir`) — однофайловый** (как `--run`): многофайловый путь скрипт без
  main НЕ поддерживает.
- **Макросы:** `stdlib/dsl.src` грузится один раз в `Context`, каждый Parser наследует Macro через
  `Context&`; `--dsl <file>` заменяет, `--no-dsl` отключает. Заголовок пакетного `.src` маскируется
  перед `ParseText`, чтобы метаданные не попадали в AST и номера строк сохранялись.
- **⚠ LSP-режим:** `allow_semantic_on_errors` разрешает семантику на частичном AST; transpile при
  ошибках не выполняется. Владение `TypeSystem` — разделяемое (`shared_ptr`): `SemanticModel`
  держит его, поэтому `TypeId` в `SemanticModel`/`SymbolIndex` остаётся валидным после Pipeline.

## Relations

- `semantic`, `ir`, `transpiler`, `solver`, `module_loader`, `session` (`Context`), `pipeline/backend`.
