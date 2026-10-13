# MEMORY.md

> scope: include/runtime/trust
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 4000

## Architecture

Публикуемый ABI-каталог компонента `runtime`: заголовки встроенных типов и рантайм-примитивов
(`any_convert`, `args`, `assert`, `big_integer`, `checked_cast`, `dict`, `enum`, `interrupt`,
`io`, `range`, `rational`, `resource`, `stack_check`, `tensor`, `tensor_backend`,
`trusted-cpp`, `trusted-cpp-sync`, `type_state`). В пакете доставляются как `trust/*.hpp` и
включаются СГЕНЕРИРОВАННЫМ кодом, а не другими компонентами компилятора.

## Facts and invariants

- **⚠ `type_state.hpp` — ЕДИНСТВЕННОЕ определение `TypeId`/`QualType`/`TypeKind`** (общий ABI:
  компилятор и сгенерированный код обязаны видеть одну раскладку); `types/typekind.hpp` только
  включает его.
- Заголовки не вшиваются в `trust-runtime.so`/`.a`: доставляются пакетом `share/trust/stdlib.trust`.
  `include/runtime` попадает в include-путь ТОЛЬКО при компиляции самой runtime-библиотеки.

## Decisions

## Relations

- `types` (модель типов читает `type_state.hpp`), `stdlib` (пакет доставки), `runtime` (реализации
  `rational`/`big_integer`/`tensor`).
