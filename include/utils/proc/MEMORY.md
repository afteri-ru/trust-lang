# MEMORY.md

> scope: include/utils/proc (+ src/utils)
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Процесс/ОС-утилиты: `elf.hpp` (работа с ELF-секциями, напр. `.debug_trust_hash`), `backtrace.hpp`
(символьный стек через `backtrace_symbols`, требует `-rdynamic`), `cache.hpp` (кеш в памяти),
`time.hpp` (время/метки).

## Facts and invariants

- **⚠ `backtrace.hpp` осмыслен только при `-rdynamic`** (флаг выставлен глобально в корневом
  `CMakeLists.txt`); без него имена функций в стеке будут пустыми.
- **⚠ Отсутствие ELF-секции — `nullopt`, не ошибка:** решение «кеш невалиден» принимает
  потребитель (`toolchain/cache`), а не `readElfSection`.
- **⚠ Время форматируется ОДНИМ кодеком `formatTimestamp`:** буфер подбирается по фактическому
  результату `strftime` (не по длине ожидаемой строки — иначе `strftime` возвращает 0 и оставляет
  неопределённое содержимое); непоместившийся формат — `FAULT`, не обрезанная строка.

## Decisions

## Relations

- `toolchain` (кеш `--run`), `sourcemap`/`package` (ELF-секции), `utils/utils.hpp` (umbrella).
