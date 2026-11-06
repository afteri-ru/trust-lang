# MEMORY.md

> scope: include/runtime/trust
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Публикуемый ABI-каталог компонента `runtime`: рантайм-МЕХАНИЗМЫ
(`any_convert`, `args`, `assert`, `checked_cast`, `enum`, `io`, `stack_check`, `type_state`).
В пакете доставляются как `trust/*.hpp` и включаются СГЕНЕРИРОВАННЫМ кодом, а не другими
компонентами компилятора. Типы-обёртки (`trusted-cpp`, `trusted-cpp-sync`, `resource`,
`interrupts`) в этот каталог НЕ входят: они лежат в `include/stdlib/core/*.hpp` и публикуются
`@stdlib/*.hpp`.
Заголовки встроенных типов (`rational`, `big_integer`, `dict`, `range`) и тензора
(`tensor`, `tensor_backend`) в этот каталог НЕ входят: они лежат рядом со своими `.src`
(`stdlib/core/builtin/*.hpp`, `stdlib/tensor/*.hpp`).

## Facts and invariants

- **⚠ `type_state.hpp` — ЕДИНСТВЕННОЕ определение `TypeId`/`QualType`/`TypeKind`** (общий ABI:
  компилятор и сгенерированный код обязаны видеть одну раскладку); `types/typekind.hpp` только
  включает его.
- **⚠ `kRuntimeAbiVersion` — версия ABI-контракта рантайма** (там же, E14/R48): упаковщик пишет её в
  `[build].runtime-abi` пакета, резолвер пакета (`PackageStore::resolveRoot`) отвергает несовпадение;
  меняется только при несовместимом изменении контракта.
- Заголовки не вшиваются в `trust-runtime.so`/`.a`: доставляются пакетом `share/trust/stdlib.trust`.
  `include/runtime` попадает в include-путь ТОЛЬКО при компиляции самой runtime-библиотеки.

## Decisions

## Relations

- `types` (модель типов читает `type_state.hpp`), `stdlib` (пакет доставки), `runtime` (реализации
  `rational`/`big_integer`/`tensor`).
