# SourceMap - хранение исходников и маппинг

## Назначение

Хранение исходных (`.src`) и выходных (`.cppt`/`.hppt`) файлов, конвертация позиций между trust- и
C++-кодом, msgpack-сериализация source-map и протокольные координаты (LSP/DAP).

## Особенности

- **Tagged-пространства позиций:** builder-space (`Mapper*`, offset от body выходного файла) vs
  reader-space (`Reader*`, offset от полного содержимого = prepend+body). `packed`-представление
  идентично, но тег запрещает кросс-передачу на уровне компилятора.
- **Source-маппинг:** конвертация offset <-> line:column (LRU-кеш поверх `SparseCache`),
  msgpack-сериализация, нормализация путей.
- **`OutputBuffer`:** prepend-буфер выходного файла (группировка по namespace, подавление дублей).
- **`protocol.hpp`:** header-only конверсии `severityToLsp`/`mapperRangeToProtocol`.
- **Чтение:** `SourceMapReader::fromElf()` из embedded ELF-секции `.debug_trust_map`; внешние файлы не используются.
