# MEMORY.md

> scope: include/solver/vc
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 4000

## Architecture

План верификации и кодирование: `vc_plan.hpp` (`VcPlan`), `smt_term_builder.hpp`, `sort_mapper.hpp`
(тип→SMT-sort).

## Facts and invariants

- **⚠ План отделён от печати:** декларации печатаются один раз, затем для КАЖДОЙ цели
  `push → аксиомы ЧУЖИХ функций → assert цели → check-sat → pop`; собственная аксиома функции в её цель
  не входит. План потребляется (`assemble(VcPlan&&)`).
- **⚠ Envelope — VC ПРОПУСКАЕТСЯ, а не доказывается:** `-Wverify-envelope` + `outsideEnvelopeCount`;
  внутренняя ошибка кодирования — `Error` и остановка; аксиома контракта строится независимо от тела.
- **Знак операторов** — из фактов типа (`resultType`/`commonType`/`lhsType`, не-целые пропускаются):
  `kIntegers` → `bvs*`, `kUnsigned` → `bvu*`.

## Relations

- `solver/impl`, `ir` (HIR/MIR), `analysis` (BehavioralModes).
