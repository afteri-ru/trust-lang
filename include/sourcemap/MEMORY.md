# MEMORY.md

> scope: include/sourcemap
> role: persistent-memory
> last_reviewed: 2026-09-16
> review_period: 30
> max_size: 8100

## Architecture

Хранение исходников (`.src`) и вывода (`.cppt`/`.hppt`), диагностики по диапазонам, маппинг
вход↔выход, msgpack-сериализация. Ключевое — **tagged-пространства позиций:** `Context` (builder,
offset от body) vs `SourceMapReader` (reader, offset от полного содержимого = prepend+body).
`packed`-представление идентично, поэтому смешение скрыто; типы параметризованы тегом, компилятор
запрещает кросс-передачу.

## Facts and invariants

- **1-based vs 0-based offset (ловушка):** source map — 1-based (первый символ = offset 1); лексер
  внутри — 0-based, поэтому при создании диапазона токена обязателен `+1`. Синтаксические ошибки bison —
  0-based (отдельный механизм). Под 1-based рассчитаны `calc_column`, `mapStart/mapStop`,
  locationToLspPosition.
- `TaggedFile`/`TaggedLocation` — НЕЗАВИСИМЫЕ битовые раскладки (бит 31 — флаг; вход 9+22, выход 5+26).
- `OutputBuffer` (`Context::output_prepend`): префиксы группируются по namespace, дубли подавляются
  (`std::set`).
- `SourceMapReader` загружается из embedded ELF-секции `.debug_trust_map` (`fromElf()`); внешние
  `.map`/`.trust` не используются.

## Decisions

- Source map выделен из `diag`: чистые диагностики/опции не зависят от хранения исходников и msgpack.

## Relations

- Зависит от `diag` (severity/protocol) и `utils` (cache/file_io/elf/zstd) + msgpack; `reader.cpp`
  использует LLVM (ArrayRef).
- Владелец `SourceMapWriter` — фасад `Context` (`session`).
