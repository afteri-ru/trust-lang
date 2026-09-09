# MEMORY.md

> scope: include/solver
> role: persistent-memory
> last_reviewed: 2026-09-15
> review_period: 30
> max_size: 5000

## Architecture

Генерация SMT-LIB 2 + опциональный Z3 для верификации контрактов. Ключевые сущности: `SmtAst/SmtOp`
(X-macro), `SmtPrinter`, `TrustToSmt`, `SolverInterface`/`SolverZ3`/`SolverStub`. Режимы
`--solver-mode`: `export` (`.smt2`+`.map`, без Z3), `calculate` (runScript → sat/unsat + контрпример),
`assert` (рантайм-проверки). VC = `(and pre (not post))`; параметры — константы с УНИКАЛЬНЫМИ именами.
`WITH_SOLVER=ON` линкует Z3, `OFF` (умолч.) — stub/текст SMT-LIB 2 (требуется Z3 ≥ 4.8).

## Facts and invariants

- **Циклы без инварианта:** `-Wsolver-loop=ignore|warning|error` (default warning) и поведенческий
  `-fsolver-loop-unroll`. Приоритет: инвариант → индукция → `z3_unroll(N)` в инварианте → глобальный
  флаг → диагностика.
- **Знак операторов:** `kIntegers` → знаковые `bvs*`/`bvsdiv`/`bvsrem`; `kUnsigned` → беззнаковые
  `bvu*`/`bvudiv`/`bvurem`; bitwise `.&.`→bvand, `.>>.`→bvashr/bvlshr, `.<.`→bvshl. Знак берётся из
  `exprSign` (по операндам-переменным; узел сравнения даёт INVALID/Bool).
- **BitVec exactness (AoRTE):** overflow/wrap учитываются (напр. `abs(INT32_MIN)` SAT) — это корректно,
  не баг.
- **Encoding:** `IfStmt`→ite; `@{A@};`/`trust_assert` → консеквенты; `WhileStmt`/`DoWhileStmt` —
  инвариант→индукция, без него→unrolling/диагностика; `CallExpr`+аксиома `∀params.pre→post`; массивы
  `select/store` (z3 4.8 нет `as const`); type-assert→`¬∀v:sort.A(v)`.
- **⚠ Ловушка (TrustElem):** AST-аргументы в `TrustElem::m_args` — грамматика копирует `m_args`
  args-терма, НЕ `m_sequence`; name_resolution связывает связку квантора во вложенном скоупе.
- **VC isolation:** `push/check-sat/pop`; первый SAT⇒SAT, все UNSAT⇒UNSAT; `kUnsupported` (stub)⇒
  `kUnsupported`, НЕ unsat. Логика `AUFBV`/`UFBV`/`QF_UFBV`.
- **SolverZ3:** `let` — inline `substLet` (z3 4.8 нет `Z3_mk_let`); Non-RC контекст.

## Decisions

## Relations
