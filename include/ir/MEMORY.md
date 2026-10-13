# MEMORY.md

> scope: include/ir (public API)
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 9000

## Architecture

IR-семейство производных представлений + движок анализов (namespace `trust::flow`):
- **HIR** — структурная форма для бэкендов: `buildHir` (клон AST + lowering, `ir/hir/build.cpp`),
  `verifyHir` (`hir/verify.cpp`), публичные `ir/hir_module.hpp`, `ir/hir_verify.hpp`, `ir/lowering.hpp`.
- **MIR/CFG** — единственный структурный вход flow-анализов и верификатора: `mir.hpp`
  (`MirOp`/`MirInstruction`), `cfg.hpp` (`BasicBlockT`/`CfgT`, `buildCfg`, `buildMir`), `places.hpp`,
  `place_root.hpp`, `verifyFlowModule` (`mir/cfg_verify.cpp`).
- **Движок**: `dataflow.hpp` (worklist/fixpoint над `CfgT<Element>`), `analysis_manager.hpp`
  (`Analysis`/`AnalysisManager`, топологический порядок).

Детали реализации — в internal-подкаталогах:

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `ir/hir/` | `hir/MEMORY.md` | lowering на клоне, контракт `verifyHir`, диагностики HIR |
| `ir/mir/` | `mir/MEMORY.md` | состав `MirOp`, построение CFG по HIR, структурный верификатор MIR |

## Facts and invariants

- **⚠ MIR — единственный вход flow-анализов и верификатора:** строится РОВНО ОДИН раз (`buildMir`),
  анализ получает готовый `MirModule` (CFG построены), а не HIR. Вход CFG — ТОЛЬКО HIR (сахар развёрнут
  lowering'ом); исходная AST не подаётся.
- **⚠ `verifyFlowModule` — единственный структурный гейт представления** (ICE-политика): потребители не
  перепроверяют инварианты формы MIR. Недостижимые блоки НЕ удаляются (как в Clang CFG) — помечаются
  `unreachable`, чтобы анализ не давал ложных срабатываний.
- **⚠ Порядок анализов наблюдаем:** `AnalysisManager` строит ТОПОЛОГИЧЕСКИЙ порядок (порядок регистрации
  сохраняется для независимых); цикл зависимостей/неизвестная зависимость — FAULT, а не тихий пропуск
  (порядок влияет на диагностики).
- **⚠ `PlaceId`/`PlaceTable` — общая идентичность места** с solver/flow-анализами (без второго словаря);
  порядок = порядок регистрации, поэтому `.smt2` байт-стабилен; таблица живёт только на время кодирования.
- **`dataflow.hpp` генерик по `Element`:** движок не знает MIR-специфики; анализ задаётся duck-typed
  классом (`State`/`direction`/`boundary`/`join`/`transfer*`), граф валидируется (`EXPECT`).
- **⚠ HIR — структурный вход бэкендов:** публичные API кодогенерации и solver'а принимают `HirModule`;
  AST (pre-lowering) — вход диагностик и LSP. HIR строится ровно ОДИН раз.
- **HIR — не SSA:** `if/while/match` структурированы; развёрнут только «сахар» (`SemicolonStmt`,
  метки/goto, временные анализатора).
- **⚠ `verifyHir` — единственный гейт контракта HIR** (детали — `ir/hir/MEMORY.md`): нарушение —
  ICE-политика (`reportInternalError`), не Warning.
- **⚠ `origin`** — единственное хранимое направление соответствия AST↔HIR (nullptr у синтетических).

## Decisions

- **HIR и MIR — одно семейство представлений в одном компоненте:** один структурный вход бэкендов (HIR),
  один вход flow-анализов/верификатора (MIR), верификаторы рядом; лишняя компонента и link-ребро не нужны.
- **Namespace кода — `trust::flow`:** имя компонента (`ir`) и namespace намеренно не совпадают;
  переименование namespace не выполняется (это отдельное механическое изменение, не часть раскладки).
- Владелец фаз — `Pipeline`, а не семантика/представления: semantic → buildHir → `verifyHir` →
  buildMir → `verifyFlowModule` → flow-анализы → бэкенды.

## Relations

- `ast` (узлы/clone/lowering), `session` (`Context`), `diag` (ICE/диагностики), `attrs` (`buildCfg`
  классифицирует readonly-цели), `semantic` (DA/borrow по MIR), `solver` (CFG/элементы/места),
  `transpiler` (HIR), `pipeline` (владелец фаз и результата).
