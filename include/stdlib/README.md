# Stdlib - стандартная библиотека TrustLang

## Назначение

Контейнер стандартной библиотеки: `stdlib_lib` (header-only interface) и самодостаточные
пакеты-подкаталоги. Пакет — каталог с `trust.toml` в корне; `include/stdlib` пакетом НЕ является
(собственного `trust.toml` нет), поэтому в архивных путях и публичном пространстве имён
`stdlib/`/`include/stdlib` он присутствует как контейнер, а не как поставляемый пакет.

## Состав

- `core/` — пакет `stdlib.trust`: модули `.src`, реализации встроенных типов `builtin/*.src` с
  их C++-заголовками `builtin/*.hpp`; публичный интерфейс — `@stdlib/…`, `@stdlib/builtin/…`.
- `tensor/` — пакет `tensor.trust`: Trust API и link-контракт с toolchain-плагином `tensor_cpu`.

## Особенности реализации

- Пакеты добавляются корневым `CMakeLists.txt` после появления target `trust`; каждый пакет сам
  регистрирует свой target в `TRUST_PACKAGE_TARGETS`.
- Реализации встроенных типов и библиотечных классов грузятся ЯВНО одним зонтичным модулем `\std();`;
  prelude-автозагрузки нет.
- Ядро — `core/MEMORY.md`; tensor — `tensor/MEMORY.md`; механизм пакетов — `include/package/MEMORY.md`.
