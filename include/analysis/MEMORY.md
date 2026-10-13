# MEMORY.md

> scope: include/analysis
> role: persistent-memory
> last_reviewed: 2026-11-03
> review_period: 30
> max_size: 4000

## Architecture

Контракт анализа для БЭКЕНДОВ и верификатора (header-only): поведенческие режимы компилятора
(`modes.hpp` — `BehavioralModes`, значения `--solver-mode`/`--stack-check`/`-Wsolver-loop`, имена
нативных функций контроля стека) и классификация рантайм-проверок (`runtime_checks.hpp` —
`RuntimeCheckKind`, `runtimeCheckOf`). Правила анализа живут в `semantic`, структура тела — в `ir`,
символы — в `symbols`.

## Facts and invariants

- **⚠ Поведенческие режимы приходят ДАННЫМИ:** значения разрешает владелец флагов `semantic`
  (`semantic::behavioralModesFromOptions`) и передаёт `BehavioralModes`; кодоген и верификатор НЕ
  обращаются к `diag::Options`/`FlagKind` — иначе появились бы зависимости `transpiler`/`solver` →
  `semantic`.
- **⚠ `runtime_checks.hpp` — единый предикат «какая проверка нужна узлу»** по ФАКТАМ семантики
  (`Binary::m_overflowCheck`, `RefTakeExpr::m_lowering`), а не по повторному выводу из типов/скоупов.
  Политика (флаги) остаётся у потребителя; проверки, которых у кодогена нет, классификатор НЕ выдумывает.
- **Имена нативных функций контроля стека — единый источник** (`analysis::stack_check_fn`): по ним
  распознаёт семантика и эмитит вызов кодоген (опечатка в одном месте исключена).

## Decisions

- Value-типы режимов принадлежат нейтральному компоненту: `transpiler`/`solver` получают данные, не
  завися от анализатора.

## Relations

- `ast` (узлы для `runtimeCheckOf`), `semantic` (владелец флагов), `transpiler`/`solver` — потребители;
  `ir` — структура тела, `symbols` — таблица символов.
