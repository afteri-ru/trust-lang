# MEMORY.md

> scope: include/stdlib/core/builtin
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 3000

## Architecture

Реализации предобъявленных встроенных типов на самом TrustLang: `string.src`, `rational.src`
(дефолтная реализация `:Rational`), `rational_openssl.src` (заменяемая реализация) и её
C++-заголовок `rational_openssl.hpp`. Загружаются по требованию, а НЕ входят в prelude.

## Facts and invariants

- **⚠ Встроенный тип — предобъявление + отдельная реализация:** здесь реализация оператором `=`
  (НЕ создаёт новый тип); `::=` создаёт новый тип и для встроенного имени запрещён.
- **⚠ `rational_openssl.src` по умолчанию НЕ загружается:** переопределяет дефолт только при
  явной загрузке/копировании в проект.

## Decisions

## Relations

- `types` (критерии встроенности), `include/stdlib/core/MEMORY.md` (доставка единым пакетом).
