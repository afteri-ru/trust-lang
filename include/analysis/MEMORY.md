# MEMORY.md

> scope: include/analysis
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Контракт анализа для БЭКЕНДОВ и верификатора (header-only): поведенческие режимы компилятора
(`modes.hpp` — `BehavioralModes`, значения `--solver-mode`/`--stack-check`/`-Wsolver-loop`, имена
нативных функций контроля стека) и классификация рантайм-проверок (`runtime_checks.hpp` —
`RuntimeCheckKind`, `runtimeCheckOf`). Правила анализа живут в `semantic`, структура тела — в `ir`,
символы — в `symbols`.

## Facts and invariants

- **⚠ Поведенческие режимы приходят ДАННЫМИ:** значения разрешают владельцы флагов —
  `semantic` (`semantic::behavioralModesFromOptions`: solver/stack-check) и `transpiler`
  (`transpiler::codegenModes`: `-foverflow-check`/`-fcomments`/`-Wbacktrace`); потребители
  (кодоген, верификатор) НЕ обращаются к `diag::Options`/`FlagKind` — иначе появились бы
  зависимости `transpiler`/`solver` → `semantic`.
- **⚠ `runtime_checks.hpp` — единый предикат «какая проверка нужна узлу»** по ФАКТАМ семантики
  (`Binary::m_overflowCheck`, `RefTakeExpr::m_lowering`), а не по повторному выводу из типов/скоупов.
  Политика (флаги) остаётся у потребителя; проверки, которых у кодогена нет, классификатор НЕ выдумывает.
- **Имена функций контроля стека — из единого реестра интринсиков** (`types/intrinsics.hpp`):
  `analysis::stack_check_fn` — константы-ссылки `intrinsicName(id)`; по ним распознаёт семантика и
  эмитит кодоген (`emitIntrinsic` → `handleStackCheckNative`); отдельных строковых списков нет.

- **⚠ `builtin_class.hpp` — единый предикат «реализация builtin генерирует C++-класс»**
  (`builtinImplGeneratesCppClass`: базисы в синтаксисе или член с телом). Читают семантика (include типа =
  заголовок модуля-владельца) и кодоген (эмиссия класса) — второй копии правила быть не должно.

## Decisions

- Value-типы режимов принадлежат нейтральному компоненту: `transpiler`/`solver` получают данные, не
  завися от анализатора.

## Relations

- `ast` (узлы для `runtimeCheckOf`), `semantic`/`transpiler` (владельцы флагов), `transpiler`/`solver` —
  потребители; `ir` — структура тела, `symbols` — таблица символов.
