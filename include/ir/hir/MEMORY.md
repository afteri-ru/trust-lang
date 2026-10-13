# MEMORY.md

> scope: include/ir/hir
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 6000

## Architecture

HIR — производная от AST структурная форма, которую читают C++-бэкенд и solver; flow-анализы читают
MIR (строится из HIR). `buildHir` = глубокий клон (`ast/clone.hpp`) + `hir::lowering` на клоне
(`ir/hir/build.cpp`, `ir/hir/lowering.cpp`). Строится в `Pipeline` ПОСЛЕ семантики. Публичные
заголовки — в корне `ir/` (`hir_module.hpp`, `hir_verify.hpp`, `lowering.hpp`); этот internal-подкаталог — lowering,
верификатор и диагностики (`ir/hir/diag.hpp`).

## Facts and invariants

- **⚠ Lowering — только в `ir/hir` и только на КЛОНЕ** (`ast/clone.hpp`): исходная AST остаётся
  pre-lowering. `captureLastResult` (`$^`) — пре-семантический проход в `semantic`.
- **⚠ `verifyHir` — единственный гейт контракта HIR:** recovery-узлов нет, тип-имена материализованы,
  каждый `goto` имеет ровно одну метку, именованные break/continue развёрнуты, у нетипизированной
  пользовательской переменной есть `inferredType`, `argCasts` согласован по арности, контролируемая
  арифметика классифицирована, синтетические временные уникальны в функции, каждый invariant-контракт
  привязан ровно к одному циклу. Нарушение — ICE-политика (`reportInternalError`), не Warning.
- **⚠ Новый синтетический префикс временной обязан попасть в `isSyntheticTempName`
  (`src/ir/hir/verify.cpp`):** иначе верификатор либо потребует `inferredType` у lowering-временной, либо
  не проверит уникальность имён.
- **Привязка инварианта — HIR-факт lowering'а:** `m_invariants` — НЕвладеющие ссылки на
  `TrustContract`-операторы, остающиеся в теле (кодоген эмитит рантайм-проверку); инвариант без цикла —
  `-Whir-invariant-unattached` (Error, `ir/hir/diag.hpp`), не тихий пропуск.
- **Prelude не попадает в `PipelineResult::hirNodes`** (`tailFrom`): иначе изменились бы счётчики
  временных и вывод модуля. Порядок узлов = порядок входа.
- **Клон ремапит невладеющие ссылки по декларативному реестру** (`TRUST_NONOWNING_SLOTS`,
  `ast/clone.cpp`); ссылки на узлы вне дерева остаются ссылками на исходную AST.
- **⚠ `origin`** — единственное хранимое направление соответствия AST↔HIR (nullptr у синтетических):
  им пользуются диагностика синтетических узлов и сопоставление фактов в кодогене.

## Decisions

- HIR — отдельная форма, а не «мутированная AST»: один структурный вход бэкендов, перезапускаемость
  анализа и сохранение исходной AST для solver/диагностик.

## Relations

- `ast` (узлы/clone/lowering), `session` (`Context`), `ir/mir` (`buildMir`), `semantic` (flow-анализы
  по MIR), `transpiler` и `solver` (потребители), `pipeline` (владелец фаз).
