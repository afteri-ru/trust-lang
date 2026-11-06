# MEMORY.md

> scope: include/ir (public API)
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

IR-семейство производных представлений + движок анализов (namespace `trust::flow`):
- **HIR** — высокоуровневая печатная форма для бэкендов: `buildHir` (клон AST + lowering), `verifyHir`; вход
  кодогенерации и solver'а (AST — вход диагностик/LSP). HIR строится РОВНО один раз.
- **MIR/CFG** — analysis/verification IR (CFG-over-places): единственный вход flow-анализов и верификатора,
  НЕ вход кодогенерации (C++-бэкенд читает HIR): `mir.hpp`, `cfg.hpp`, `places.hpp`, `verifyFlowModule`.
- **Движок**: `dataflow.hpp` (worklist/fixpoint над `CfgT<Element>`), `analysis_manager.hpp`
  (топологический порядок).

Детали реализации — в internal-подкаталогах:

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `ir/hir/` | `hir/MEMORY.md` | lowering на клоне, контракт `verifyHir`, диагностики HIR |
| `ir/mir/` | `mir/MEMORY.md` | состав `MirOp`, построение CFG по HIR, структурный верификатор MIR |

## Facts and invariants

- **⚠ MIR — единственный вход flow-анализов и верификатора:** строится один раз (`buildMir`), анализ получает
  готовый `MirModule` (CFG построены), не HIR. Вход CFG — ТОЛЬКО HIR (сахар развёрнут lowering'ом); AST не
  подаётся.
- **⚠ `verifyFlowModule` — единственный структурный гейт представления** (ICE-политика); потребители не
  перепроверяют форму MIR. Недостижимые блоки НЕ удаляются (как Clang CFG) — помечаются `unreachable`, чтобы
  анализ не давал ложных срабатываний.
- **⚠ Порядок анализов наблюдаем:** `AnalysisManager` строит ТОПОЛОГИЧЕСКИЙ порядок (регистрация сохраняется
  для независимых); цикл/неизвестная зависимость — FAULT, а не тихий пропуск.
- **⚠ `PlaceId`/`PlaceTable` — общая идентичность места** с solver/flow (без второго словаря); порядок =
  порядок регистрации, поэтому `.smt2` байт-стабилен; таблица живёт только на время кодирования.
- **`dataflow.hpp` генерик по `Element`:** движок не знает MIR; анализ — duck-typed класс
  (`State`/`direction`/`boundary`/`join`/`transfer*`), граф валидируется (`EXPECT`).
- **⚠ HIR — вход бэкендов, не SSA:** `if/while/match` структурированы; развёрнут только «сахар»
  (`SemicolonStmt`, метки/goto, временные анализатора).
- **⚠ `verifyHir` — единственный гейт контракта HIR** (детали — `ir/hir/MEMORY.md`): нарушение — ICE.
- **⚠ `origin`** — единственное хранимое соответствие AST↔HIR (nullptr у синтетических).

## Decisions

- **HIR и MIR — одно семейство в одном компоненте:** один структурный вход бэкендов (HIR), один вход
  flow/верификатора (MIR); лишняя компонента и link-ребро не нужны.
- **Namespace кода — `trust::flow`:** имя компонента (`ir`) и namespace намеренно не совпадают.
- Владелец фаз — `Pipeline`: semantic → buildHir → `verifyHir` → buildMir → `verifyFlowModule` →
  flow-анализы → бэкенды.

## Relations

- `ast` (узлы/clone/lowering), `session`, `diag` (ICE), `attrs` (`buildCfg` классифицирует readonly-цели),
  `semantic` (DA/borrow по MIR), `solver` (CFG/места), `transpiler` (HIR), `pipeline` (владелец фаз).
