# SourceMap - хранение исходников и маппинг

## Назначение

Хранение исходных (`.src`) и выходных (`.cppt`/`.hppt`) файлов, конвертация позиций между trust- и
C++-кодом, msgpack-сериализация source-map и протокольные координаты (LSP/DAP).

## Состав

- **`file_entry.hpp`** - `FileEntry`: имя/содержимое файла, хэш, кеш line/column.
- **`source_map.hpp`** - шаблонная база `SourceMap<FileIdx>`: файлы, маппинги, LRU-кеши.
- **`output_buffer.hpp`** - `OutputBuffer` (prepend-буфер выходного файла).
- **`writer.hpp`** - `SourceMapWriter`: входные/выходные файлы, prepend, `mapStart`/`mapStop`,
  регистрация маппингов, `toReader`.
- **`reader.hpp`** - `SourceMapReader`: чтение/поиск диапазонов и имён, msgpack-сериализация.
- **`mapper.hpp`** - зонтик над перечисленными заголовками + `SourceLocation` («файл:строка»).
- **`protocol.hpp`** - header-only конверсии `severityToLsp`/`mapperRangeToProtocol` (LSP/DAP).

## Реализация

Writer-сторона разложена по TU: `output_buffer.cpp`, `writer_files.cpp`, `writer_mapping.cpp`,
`writer_to_reader.cpp`; reader-сторона: `reader.cpp`, `reader_codec.cpp`, `reader_lookup.cpp`.

## Особенности

- **Tagged-пространства позиций:** builder-space (`Mapper*`, offset от body выходного файла) vs
  reader-space (`Reader*`, offset от полного содержимого = prepend+body). `packed`-представление
  идентично, но тег запрещает кросс-передачу на уровне компилятора.
- **Source-маппинг:** конвертация offset <-> line:column (LRU-кеш поверх `SparseCache`),
  msgpack-сериализация, нормализация путей.
- **`OutputBuffer`:** prepend-буфер выходного файла (группировка по namespace, подавление дублей).
- **`protocol.hpp`:** header-only конверсии `severityToLsp`/`mapperRangeToProtocol`.
- **Чтение:** `SourceMapReader::fromElf()` из embedded ELF-секции `.debug_trust_map`; внешние файлы не используются.
