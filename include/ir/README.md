# IR - HIR/MIR, CFG и движок flow-анализов

## Назначение

Семейство производных представлений и анализов:
- **HIR** — структурная форма для бэкендов (клон AST + lowering; `ir/hir_module.hpp`,
  `ir/hir_verify.hpp`, `ir/lowering.hpp`).
- **MIR** — структура тела функции с CFG: единственный вход flow-анализов и верификатора.
- **Движок** — `dataflow.hpp` + `analysis_manager.hpp`.

Потребители — `semantic` (DA/borrow по MIR), `solver` (CFG/элементы/места), `transpiler` (HIR),
`pipeline` (владелец фаз). Кодоген MIR не читает. Детали реализации — `ir/hir/README.md`,
`ir/hir/MEMORY.md`, `ir/mir/MEMORY.md`.

## Состав

- **`mir.hpp`** - `MirOp`/`MirInstruction`: одно действие над местом в семантическом порядке
  (`Declare`/`Read`/`Write`/`Move`/`Call`/`ScopeExit`/`Assert`/`Invariant`); `place` — ключ `PlaceTable`,
  `site` — источник, `scope` — скоуп объявления/выхода, `projection` — lvalue поля/элемента.
- **`cfg.hpp`** - CFG над MIR: `BasicBlockT<Element>`/`CfgT<Element>`, алиасы
  `FlowBlock`/`ControlFlowGraph`, `MirFunction`/`MirModule`; `buildCfg(FuncDecl, AttrPool)` и
  `buildMir(roots, AttrPool)`. Вход — HIR (сахар развёрнут lowering'ом), `SemicolonStmt` прозрачен,
  goto-метки разрешаются отложенно, `ScopeExit` вычисляется по снимкам стека скоупов.
- **`cfg_verify.cpp`** - `verifyFlowModule(mir)`: структурный гейт представления (ICE).
- **`places.hpp`** - `PlaceId`/`PlaceTable<Key>`: плотные стабильные идентификаторы мест.
- **`place_root.hpp`** - `placeRoot(node)`: корневое место (Ident/поле/элемент/`&`/`*`) по `resolvedDecl`.
- **`dataflow.hpp`** - header-only worklist/fixpoint над `CfgT<Element>`: forward/backward, join,
  per-edge effects; анализ задаётся duck-typed классом.
- **`analysis_manager.hpp`** - `Analysis`/`AnalysisManager`: имя + зависимости, топологический порядок,
  неизвестная зависимость/цикл — FAULT; единственная точка запуска flow-анализов.

## Принцип

MIR — единственный структурный вход flow-анализов и верификатора; исходная AST остаётся для диагностик
и LSP. Инварианты — `MEMORY.md`.
