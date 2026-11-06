# MEMORY.md

> scope: include/ir/hir
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

HIR — производная от AST высокоуровневая печатная форма, которую читают C++-бэкенд и solver; flow-анализы
читают MIR (строится из HIR). `buildHir` = глубокий клон (`ast/clone.hpp`) + `hir::lowering` на клоне.
Строится в `Pipeline` ПОСЛЕ семантики. Публичные заголовки — в корне `ir/`; этот internal-подкаталог —
lowering, верификатор и диагностики (`ir/hir/diag.hpp`).

## Facts and invariants

- **⚠ Lowering — только в `ir/hir` и только на КЛОНЕ:** исходная AST остаётся pre-lowering.
  `captureLastResult` (`$^`) — пре-семантический проход в `semantic`.
- **⚠ `verifyHir` — единственный гейт контракта HIR:** recovery-узлов нет, тип-имена материализованы, каждый
  `goto` имеет одну метку, именованные break/continue развёрнуты, у нетипизированной переменной есть
  `inferredType`, `argCasts` согласован по арности, контролируемая арифметика классифицирована, временные
  уникальны в функции, каждый invariant привязан ровно к одному циклу. Нарушение — ICE, не Warning.
- **⚠ Новый синтетический префикс временной обязан попасть в `isSyntheticTempName` (`src/ir/hir/verify.cpp`):**
  иначе верификатор либо потребует `inferredType` у lowering-временной, либо не проверит уникальность имён.
- **Привязка инварианта — HIR-факт lowering'а:** `m_invariants` — НЕвладеющие ссылки на `TrustContract`-
  операторы, остающиеся в теле (кодоген эмитит проверку); инвариант без цикла — `-Whir-invariant-unattached`
  (Error), не тихий пропуск.
- **Prelude не попадает в `PipelineResult::hirNodes`** (`tailFrom`): иначе изменились бы счётчики временных и
  вывод модуля. Порядок узлов = порядок входа.
- **Клон ремапит невладеющие ссылки по декларативному реестру** (`TRUST_NONOWNING_SLOTS`); ссылки на узлы вне
  дерева остаются ссылками на исходную AST.
- **⚠ `origin`** — единственное хранимое соответствие AST↔HIR (nullptr у синтетических): им пользуются
  диагностика и сопоставление фактов в кодогене.

## Decisions

- HIR — отдельная форма, а не «мутированная AST»: один структурный вход бэкендов, перезапускаемость анализа,
  сохранение исходной AST для solver/диагностик.

## Relations

- `ast` (узлы/clone/lowering), `session`, `ir/mir` (`buildMir`), `semantic` (flow по MIR), `transpiler` и
  `solver` (потребители), `pipeline` (владелец фаз).
