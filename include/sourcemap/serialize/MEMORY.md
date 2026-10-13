# MEMORY.md

> scope: include/sourcemap/serialize
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 4000

## Architecture

`reader.hpp` + `reader_codec`/`reader_lookup` (чтение embedded source map), `msgpack_util.hpp`
(кодек). Запись — `writer*.cpp` (корень `src/sourcemap`).

## Facts and invariants

- **`SourceMapReader` загружается из embedded ELF-секции `.debug_trust_map` (`fromElf()`);** внешние
  `.map`/`.trust` не используются.
- **msgpack-типы нужны только reader-стороне:** writer объявляет reader вперёд и не включает msgpack.
- **⚠ Tagged-пространства позиций:** `Context` (builder, offset от body) и `SourceMapReader` (reader,
  offset от полного содержимого = prepend+body); `packed`-представление идентично, типы параметризованы
  тегом и компилятор запрещает кросс-передачу.

## Relations

- `sourcemap/store`, `utils` (cache/file_io/elf/zstd), `debug` (чтение карты).
