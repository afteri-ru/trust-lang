# MEMORY.md

> scope: include/trust
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Компонент `include/trust` содержит ТОЛЬКО инфраструктуру сборки самого КОМПИЛЯТОРА: C++20-модуль
`trust` (PCM/объект собираются здесь: `trust.pcm`, `trust_module.o`) и генерируемый заголовок версии
`version.h`. Рантайм-заголовки ВСТРОЕННЫХ типов — в `include/runtime/trust/`; исходники и заголовки
СТАНДАРТНОЙ БИБЛИОТЕКИ — в `include/stdlib/`.

## Facts and invariants

- **Общий ABI-заголовок типов:** `TypeId`/`QualType` определены в `include/runtime/trust/type_state.hpp`
  и подключаются компилятором как `runtime/trust/type_state.hpp` (через `-I${PROJECT_INCLUDE_DIR}`).
  Поэтому `TRUST_COMPILE_OPTIONS` НЕ добавляет `-I${PROJECT_INCLUDE_DIR}/runtime`: прочие
  переопределяемые runtime-заголовки компилятору не видны (и `utils/error.hpp` от них не зависит).

## Decisions

## Relations

- Встроенные типы, рантайм-заголовки и критерий встроенности — `include/runtime/MEMORY.md`.
- Стандартная библиотека — `include/stdlib/MEMORY.md` (контейнер), ядро — `core/MEMORY.md`.
