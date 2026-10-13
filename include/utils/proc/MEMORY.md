# MEMORY.md

> scope: include/utils/proc (+ src/utils)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 3000

## Architecture

Процесс/ОС-утилиты: `elf.hpp` (работа с ELF-секциями, напр. `.debug_trust_hash`), `backtrace.hpp`
(символьный стек через `backtrace_symbols`, требует `-rdynamic`), `cache.hpp` (кеш в памяти),
`time.hpp` (время/метки).

## Facts and invariants

- **⚠ `backtrace.hpp` осмыслен только при `-rdynamic`** (флаг выставлен глобально в корневом
  `CMakeLists.txt`); без него имена функций в стеке будут пустыми.
- **⚠ Отсутствие ELF-секции — `nullopt`, не ошибка:** решение «кеш невалиден» принимает
  потребитель (`toolchain/cache`), а не `readElfSection`.

## Decisions

## Relations

- `toolchain` (кеш `--run`), `sourcemap`/`package` (ELF-секции), `utils/utils.hpp` (umbrella).
