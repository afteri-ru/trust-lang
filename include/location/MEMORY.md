# MEMORY.md

> scope: include/location
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 4915

## Architecture

Header-only (`location_lib`): примитивы позиций и идентификаторов файлов, параметризованные
**тегом пространства** — builder-space `MapperFile` vs reader-space `ReaderFile`. Используется
`diag` (`Context`/`SourceMapReader`) и всеми, кто оперирует диапазонами. Зависит только от
`utils` (макросы `FAULT`/`EXPECT`), нижестоящих компонентов нет.

## Facts and invariants

- **Ключевой инвариант — разные смыслы offset'ов у `Mapper*` и `Reader*`:** `Mapper*` — offset
  относительно **body** выходного файла (builder-space); `Reader*` — относительно **полного
  содержимого** (prepend + body). `packed`-представление идентично, поэтому смешение пространств
  скрыто на битовом уровне. Компилятор запрещает неявное смешение через `Tag`-маркер
  (кросс-теговая конверсия — только `explicit`-конструктор / `static_cast`).
- Упаковка `uint32_t`: бит 31 — флаг выходного файла; входной файл 9 бит index (1..511) + 22 бита
  offset (~4MB); выходной 5 бит index (1..31) + 26 бит offset (~64MB). Границы зафиксированы
  `static_assert`/`EXPECT`.
- Значения — POD, передаются по значению, не владеют ничем.
