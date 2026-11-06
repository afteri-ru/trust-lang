# MEMORY.md

> scope: include/solver
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

SMT-LIB 2 + опциональный Z3 (`WITH_SOLVER`, иначе stub). Слои: план `VcPlan` → печать `SmtAssembler` →
исполнение `runScript` + `VerificationReport`. Режимы `--solver-mode`: `export`, `calculate`, `assert`. Детали
и envelope — `solver/README.md`, `VERIFICATION_ENVELOPE.md`; реализация — `vc/`, `smt/`, `impl/`.

## Facts and invariants

- **⚠ План отделён от печати:** декларации печатаются один раз, затем для КАЖДОЙ цели
  `push → аксиомы ЧУЖИХ функций → assert цели → check-sat → pop`; собственная аксиома функции в её цель не
  входит. План потребляется (`assemble(VcPlan&&)`).
- **⚠ Данные — HIR, структура — MIR:** HIR — декларации/контракты и материализованные семантикой факты; MIR —
  CFG/элементы/места. Резолва по тексту нет; режимы — данными (`analysis::BehavioralModes`); зависимости от
  `semantic` нет.
- **⚠ Состояние — значения МЕСТ (`flow::PlaceId`):** порядок = порядок в `PlaceTable`, поэтому `.smt2`
  байт-стабилен; таблица живёт только на время `encodeBody`. Чтение места без значения — ICE. При затенении
  (`s := s + 1`) именем разрешается ПРЕДЫДУЩЕЕ место с тем же текстом.
- **⚠ Циклы:** инварианты — `MirOp::Invariant`; `while` требует establishment + сохранения от havoc mod-set,
  `do-while` — первая итерация безусловна. `z3_unroll(N)` — bounded unrolling; без инварианта и unroll —
  envelope (severity `-Wsolver-loop`).
- **⚠ Envelope — VC ПРОПУСКАЕТСЯ, не доказывается:** `-Wverify-envelope` + `outsideEnvelopeCount`; внутренняя
  ошибка кодирования — `Error` и остановка; аксиома контракта строится независимо от тела.
- **Знак операторов** — из фактов типа (не-целые пропускаются): `kIntegers` → `bvs*`, `kUnsigned` → `bvu*`.
- **⚠ Ловушка `TrustElem`:** AST-аргументы в `m_args` — грамматика копирует `m_args` args-терма, НЕ
  `m_sequence`; связка квантора — во вложенном скоупе.
- **Отчёт:** проверяются ВСЕ цели (первый SAT не останавливает), `kUnsupported` (stub) ≠ unsat.
- **⚠ Верификатор/сортировка читают read-model порт `RunServices` (`session/run_services.hpp`), а НЕ `Context`:**
  `TrustToSmt`/`SortMapper`/`SmtPrinter::buildSmt2Map` получают порт (доступны `readTypes()`/`diag()`/`opts()`/`source()`);
  мутабельный реестр `types` верификатору недостижим (R12/E16).

## Decisions

- Идентичность места — общая с flow-анализами (`flow::PlaceId`/`PlaceTable`), без второго словаря.
- Контракты формулируются на уровне источника (`FuncDecl::m_trust` в HIR).

## Relations

- `ir` (HIR/MIR/CFG), `analysis` (BehavioralModes), `session`, `diag`, `sourcemap`; НЕ зависит от `semantic`
  и `pipeline`. Стадия-обёртка SMT — `pipeline/backend_solver.cpp`; исполнение скрипта — driver.
