# MEMORY.md

> scope: include/solver/impl
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Реализация бэкенда: `smt_assembler.hpp` (печать SMT-LIB), `solver_z3.hpp`/`solver_stub.hpp` (Z3 или
stub), `diag.hpp` (диагностики solver), `impl/solver.cpp` (фасад `createSolver`/`runScript`) и
`impl/trust_to_smt.cpp` (мост AST→SMT-LIB).

## Facts and invariants

- **`WITH_SOLVER`:** при выключенном Z3 — stub; `kUnsupported` (stub) ≠ unsat.
- **Отчёт:** проверяются ВСЕ цели (первый SAT не останавливает).
- **⚠ Ловушка `TrustElem`:** AST-аргументы в `m_args` — грамматика копирует `m_args` args-терма, НЕ
  `m_sequence`; связка квантора — во вложенном скоупе.
- **Циклы:** инварианты — `MirOp::Invariant`; `while` требует establishment + сохранения от havoc mod-set,
  `do-while` — первая итерация безусловна; `z3_unroll(N)` — bounded unrolling; без инварианта и unroll —
  envelope (`-Wsolver-loop`).

## Relations

- `solver/vc`, `ir`, `diag`.
