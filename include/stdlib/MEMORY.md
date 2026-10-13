# MEMORY.md

> scope: include/stdlib
> role: persistent-memory
> last_reviewed: 2026-10-20
> review_period: 30
> max_size: 2000

## Architecture

`include/stdlib` — КОНТЕЙНЕР стандартной библиотеки, не пакет: собственного `trust.toml` нет.
Он держит `stdlib_lib` (header-only interface) и самодостаточные пакеты-подкаталоги:

- `core/` — `stdlib.trust`: prelude/модули `.src`, `builtin/*.src`, C++-заголовки библиотечных
  типов; архивный namespace остаётся `stdlib/` и `include/stdlib/` (публичный `@stdlib/…`);
- `tensor/` — `tensor.trust`: Trust API и link-контракт с toolchain-плагином `tensor_cpu`.

## Facts and invariants

- Пакет = каталог с `trust.toml` в корне. `include/stdlib` пакетом не является; пакеты
  добавляются root CMake после появления target `trust`.
- `core/` — только физическая раскладка исходников; архивные пути и публичное пространство
  имён `stdlib`/`include/stdlib` не меняются.
- Каждый пакет сам регистрирует свой target в `TRUST_PACKAGE_TARGETS`.

## Decisions

- `tensor` находится в дереве stdlib, потому что его потребитель — конечное приложение, а не
  компилятор.

## Relations

- Ядро stdlib — `core/MEMORY.md`; tensor — `tensor/MEMORY.md`.
- Механизм пакетов — `include/package/MEMORY.md`.