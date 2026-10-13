# MEMORY.md

> scope: include/location
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 5000

## Architecture

Header-only: примитивы позиций и идентификаторов файлов, параметризованные **тегом пространства** —
builder-space `MapperFile` vs reader-space `ReaderFile`. Используется `diag` и всеми, кто оперирует
файлами/позициями. `module_index.hpp` — strong-type идентичности модуля (`ModuleIndex`, `kInvalid`);
`module_name.hpp` — единая формула C++-имени модуля (encoder + `ModuleNameLookup`), см. Facts.
диапазонами. Зависит только от `utils`; нижестоящих компонентов нет.

## Facts and invariants

- **`ModuleIndex` — strong type, `kInvalid` ≠ 0:** `{0}` — валидный главный модуль, поэтому
  «модуля нет» выражается только `ModuleIndex::invalid()`/`isValid()`.
- **⚠ Один encoder имени модуля:** `encodeModuleNameForIdent` (+`moduleEntryIdent`,
  `moduleCppNamespace`) — тип scope входит в имя (`m_` группа, `p_` пакет), `_`→`__`; повторять
  формулу в других компонентах запрещено. `moduleCppNamespace` ПУСТ для неимпортируемого юнита
  (headerless root): изоляция — только импортируемые файлы, и это единственный механизм.
- **`ModuleNameLookup` — невладеющий провайдер** логического имени; реализация — `ModuleResolver`.
- **`isValidModuleLogicalName` — единый валидатор логического имени модуля** (1–3 ведущих `\`,
  компоненты через `\`, строчные латинские/цифры/`_`, без пустых компонентов и краевых `_`);
  живёт здесь, чтобы `module_loader` не зависел от `syntax` ради этой проверки.

- **⚠ Ключевой инвариант — разные смыслы offset'ов у `Mapper*` и `Reader*`:** `Mapper*` — offset
  относительно **body** выходного файла (builder-space); `Reader*` — относительно **полного
  содержимого** (prepend + body). `packed`-представление идентично, поэтому смешение пространств
  скрыто на битовом уровне. Компилятор запрещает неявное смешение через `Tag`-маркер (кросс-теговая
  конверсия — только `explicit`-конструктор / `static_cast`).
- Упаковка `uint32_t`: бит 31 — флаг выходного файла; входной файл 9 бит index (1..511) + 22 бита
  offset (~4MB); выходной 5 бит index (1..31) + 26 бит offset (~64MB). Границы зафиксированы
  `static_assert`/`EXPECT`.
- Значения — POD, передаются по значению, не владеют ничем.

## Decisions

## Relations
