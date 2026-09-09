# MEMORY.md

> scope: include/precompiled
> role: persistent-memory
> last_reviewed: 2026-09-18
> review_period: 30
> max_size: 4100

## Architecture

Компонент содержит PCH-заголовки (предкомпилированные общие префиксы), не код. `torch_pch`
(WITH_TORCH) — заголовки libtorch; `trust_pch` (TRUST_USE_PCH) — стабильный общий префикс проекта
(STL + `ast_nodes.hpp`/`registry.hpp`/`symbol_table.hpp`/`strings.hpp`). Каждый PCH собирается
custom-командой (`-x c++-header`, флаги как у TU) и подключается через INTERFACE-таргет
`-include-pch`. `trust_pch` определён ВСЕГДА (no-op при OFF); PCH-файл и `trust_pch_target` —
только при TRUST_USE_PCH.

## Facts and invariants

- **PCH — только оптимизация, НЕ несущая:** каждый TU обязан собираться с `TRUST_USE_PCH=OFF`
  (PCH маскирует отсутствующий `#include`). OFF-конфиг — обязательный верификационный.
- **Release ОБЯЗАН `TRUST_USE_PCH=OFF`:** `TRUST_USE_PCH=ON` в release → `FATAL_ERROR`
  (артефакт собирается только из явных include); `trust_pch_target` в release не создаётся.
- **PCH намеренно НЕ включает `trust/version.h`** (его тянут `syntax/term.h`/`session/context.hpp`):
  в dev версия содержит git-хеш → включение инвалидировало бы PCH на каждый коммит.
- **Актуальность PCH — через DEPFILE (`-MD -MF`)**: PCH пересобирается при изменении любого
  транзитивно включённого заголовка; без этого возможен устаревший PCH.
- `trust_pch` линкуется `PRIVATE` к компилирующим библиотекам — не пропагируется в модульные
  потребители (`unit_tests` с `-fmodule-file`).

## Decisions

- `trust_pch` всегда INTERFACE, чтобы компоненты линковали его безусловно (без `if(TARGET ...)`).
- Сборка PCH без `-fno-modules` (в отличие от torch_pch): совпадение языковых опций с TU важнее.

## Relations

- `utils/strings.hpp`, `ast/ast_nodes.hpp`, `types/registry.hpp`, `analysis/symbol_table.hpp`
  включены в stable PCH; изменение любого → пересборка PCH и всех потребителей.

