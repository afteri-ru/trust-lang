# MEMORY.md

> scope: include/trust
> role: persistent-memory
> last_reviewed: 2026-09-14
> review_period: 30
> max_size: 4000

## Architecture

Компонент `include/trust` содержит ТОЛЬКО инфраструктуру сборки самого КОМПИЛЯТОРА: C++20-модуль
`trust` (PCM/объект собираются здесь: `trust.pcm`, `trust_module.o`) и генерируемый заголовок версии
`version.h`. Рантайм-заголовки ВСТРОЕННЫХ типов — в `include/runtime/trust/`; исходники и заголовки
СТАНДАРТНОЙ БИБЛИОТЕКИ — в `include/stdlib/`.

## Facts and invariants

- **⚠ `TRUST_COMPILE_OPTIONS` (сборка `trust.pcm`) должны включать И `-I${PROJECT_INCLUDE_DIR}`,
  И `-I${PROJECT_INCLUDE_DIR}/runtime`:** заголовки встроенных типов публикуются как `trust/<h>.hpp`,
  но физически лежат в `include/runtime/trust/` (иначе падает precompile модуля —
  `utils/error.hpp` → `trust/assert.hpp`).

## Decisions

## Relations

- Встроенные типы, рантайм-заголовки и критерий встроенности — `include/runtime/MEMORY.md`.
- Стандартная библиотека — `include/stdlib/MEMORY.md`.
