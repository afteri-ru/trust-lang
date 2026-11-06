# MEMORY.md

> scope: include/syntax/util
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Внутренние диагностики синтаксиса (`diag.hpp`, `TRUST_DIAG_SET`) и PRAGMA-обёртки подавления
предупреждений (`warning_push.h`/`warning_pop.h`), используемые TU компонента.

## Facts and invariants

- **Диагностики объявляются через `TRUST_DIAG_SET`** (пер-компонентный enum) и регистрируются
  централизованно; тексты — единый источник для `-W`-опций.
- **`warning_push.h`/`warning_pop.h` — только внутренние pragma-обёртки syntax;** публичными не являются,
  из других компонентов не включаются (`internal_boundary_guard`).

## Relations

- `diag` (реестр опций), `syntax/parser`, `syntax/lexer`, `syntax/macro`.
