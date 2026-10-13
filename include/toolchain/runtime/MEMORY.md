# MEMORY.md

> scope: include/toolchain/runtime (+ src/toolchain/runtime)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 3000

## Architecture

Internal-подкаталог `toolchain`: поиск `trust-runtime` и toolchain-библиотек
(`src/toolchain/runtime/runtime_locator.cpp`). Публичный API internal-подкаталога — `toolchain/runtime/runtime_locator.hpp`.

## Facts and invariants

- **⚠ Поиск идёт от каталога РЕАЛЬНОГО исполняемого файла** (`/proc/self/exe`, симлинки
  разрешаются) по относительным путям `<base>/{lib, ../lib}/trust-runtime.{a,so}`: копия
  бинарника в другой каталог находит другую (или не находит) рантайм-библиотеку.
- **`RuntimeLink::Static` по умолчанию в сгенерированных программах** — `.so` нужен только при
  явном выборе shared-варианта.

## Decisions

## Relations

- `toolchain/build` (линковка), `package` (layout/toolchain-артефакты), `utils` (поиск по FS).
