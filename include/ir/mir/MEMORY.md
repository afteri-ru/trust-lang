# MEMORY.md

> scope: include/ir/mir
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

MIR — analysis/verification IR (CFG-over-places): структура тела функции в семантическом порядке; не
вход кодогенерации (C++-бэкенд читает HIR), её потребители — flow-анализы и верификатор. `mir.hpp` (`MirOp`/`MirInstruction`:
`Declare`/`Read`/`Write`/`Move`/`Call`/`ScopeExit`/`Assert`/`Invariant`; `place` — ключ `PlaceTable`,
`site` — источник, `scope` — скоуп объявления/выхода, `projection` — lvalue поля/элемента).
`cfg.hpp` — CFG над MIR (`BasicBlockT`/`CfgT`, `buildCfg(FuncDecl, AttrPool)`, `buildMir(roots, AttrPool)`);
`mir/cfg.cpp` — построение, `mir/cfg_verify.cpp` — `verifyFlowModule`. Публичные заголовки — в корне
`ir/` (`cfg.hpp`, `places.hpp`, `place_root.hpp`, `dataflow.hpp`, `analysis_manager.hpp`).

## Facts and invariants

- **Вход CFG — только HIR:** сахар уже развёрнут lowering'ом; `SemicolonStmt` прозрачен, goto-метки
  разрешаются отложенно, `ScopeExit` вычисляется по снимкам стека скоупов.
- **⚠ `verifyFlowModule` — единственный структурный гейт MIR** (ICE-политика): потребители не
  перепроверяют форму. Недостижимые блоки не удаляются — помечаются `unreachable`.
- **`placeRoot(node)`** — корневое место (Ident/поле/элемент/`&`/`*`) по `resolvedDecl`.
- **`PlaceId`/`PlaceTable<Key>`** — плотные стабильные идентификаторы мест; порядок = порядок
  регистрации (параметры → блоки/элементы → …), поэтому кодирование solver'а байт-стабильно.
- **MIR строится ровно один раз** (`buildMir`); flow-анализы и верификатор получают готовый `MirModule`.

## Relations

- `ir/hir` (вход), `ir` (движок и публичные заголовки), `attrs` (`buildCfg` классифицирует
  readonly-цели), `semantic` (DA/borrow), `solver` (CFG/элементы/места).
