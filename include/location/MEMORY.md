# MEMORY.md

> scope: include/location
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 4096

# Location — низкоуровневые примитивы позиций

## Назначение

Header-only компонент (`location_lib`) с примитивами позиций и идентификаторов файлов,
параметризованными **тегом пространства** (builder-space `MapperFile` vs reader-space `ReaderFile`).
Используется `diag` (`Context` / `SourceMapReader`) и всеми, кто оперирует диапазонами.

Зависимости: только `utils` (макросы `FAULT`/`EXPECT`). Нижестоящих компонентов нет.

## Типы

- **`LocationPack`** — константы битовой упаковки `uint32_t`:
  - бит 31 — флаг выходного файла (`OUTPUT_FILE_BIT`);
  - входной файл: 9 бит index (1..511) + 22 бита offset (~4MB);
  - выходной файл: 5 бит index (1..31) + 26 бит offset (~64MB).
  Инварианты зафиксированы `static_assert`.
- **`TaggedFile<Tag>`** — идентификатор файла (`raw: uint32_t`): `make_input`/`make_output`,
  `isInvalid`/`isOutput`/`as_index`. `MapperFileTag`/`ReaderFileTag` → `MapperFile`/`ReaderFile`.
- **`TaggedLocation<Tag>`** — позиция (подтип `Location`) + вложенный `RangeType {begin, end}`.
  Упаковка в `packed`; сравнение/арифметика по offset; `makeLoc`/`fromPacked`/`inc`/`dec`.
  Псевдонимы: `MapperLocation`/`MapperRange`, `ReaderLocation`/`ReaderRange`.

## Безопасность (tagged-пространства)

- `Tag` — тип-маркер. Компилятор запрещает неявное смешение `Mapper*` и `Reader*`:
  кросс-теговая конверсия — только через `explicit`-конструктор / `static_cast`.
- Все границы (число файлов, offset, `begin <= end`) проверяются `EXPECT`/`static_assert`.
- Значения — POD, передаются по значению, не владеют ничем.

## Ключевой инвариант

`Mapper*` — offset относительно **body** выходного файла (builder-space); `Reader*` — относительно
**полного содержимого** (prepend + body). Смысл offset'ов разный, хотя packed-представление
идентично; смешение пространств предотвращено на уровне типов.
