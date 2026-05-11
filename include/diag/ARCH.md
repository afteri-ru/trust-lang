# Diag — архитектура

## Назначение

Модуль `diag` предоставляет инфраструктуру для:
- хранения исходного кода (входные `.src` файлы) и буферизированного вывода (выходные `.cpp` файлы);
- диагностики (сообщения об ошибках, предупреждениях, заметках) с привязкой к диапазоном позиций в исходном коде, которые можно пересчитать в номер и позицию в строке;
- маппинга позиций между входными (src) и выходными (cpp) файлами, включая маппинг имён (переменных, функций и других именованных объектов) с последующей сериализацией для использования при отладке.

## Структура `Location`

Константы формата Location (`FILE_BITS = 11`, `OFFSET_BITS = 20`, `MAX_OFFSET`) вынесены в отдельную нешаблонную структуру `Location` для прямого доступа без инстанцирования шаблонов. Используются всеми tagged-типами (`TaggedFileIdx`, `TaggedLocation`) и клиентским кодом.

Метод `FileEntry::size()` возвращает `uint32_t` для единообразия с форматом Location, с `EXPECT`-проверкой превышения `MAX_OFFSET`.

## Ключевое архитектурное решение: tagged-пространства

В системе существуют **два пространства** для Location:

1. **Builder space** — `Context` (до `toReader()`): `FileIdx`, offset относительно **body** выходного файла (без prepend).
2. **Reader space** — `Mapper` (после `toReader()`): `ReaderFileIdx`, offset относительно **full content** (prepend + body).

Смешение этих пространств — частая ошибка, так как offset'ы имеют разный смысл (body-relative vs full-content-relative), хотя `packed`-представление идентично.

**Решение**: все типы, связанные с позицией, параметризованы тегом пространства `FileIdx`:
- `TaggedLocation<FileIdx>` — позиция в пространстве `FileIdx`
- `Mapper<FileIdx>` — данные маппинга для пространства `FileIdx`
- `DiagnosticEngine<FileIdx>` — диагностика для пространства `FileIdx`

`Context` наследует `Mapper<FileIdx>` (builder space), `SourceMapReader` наследует `Mapper<ReaderFileIdx>` (reader space). Компилятор запрещает передачу ReaderLocation в Context API и наоборот.

## Основные компоненты

### `TaggedFileIdx<Tag>` / `FileIdx` / `ReaderFileIdx`

`TaggedFileIdx<Tag>` — шаблонный tagged-класс — единый идентификатор файла (входного или выходного). `Context` и `Mapper` используют разные tagged-типы (`FileIdx` и `ReaderFileIdx`), что предотвращает смешивание индексов на уровне типов. Упакован в `uint32_t raw`:
- `raw = 0` — невалидный;
- `raw > 0`, бит `FILE_BITS` (11-й) = 0 — входной: `raw = index + 1`;
- `raw > 0`, бит `FILE_BITS` = 1 — выходной: `raw = (index + 1) | (1 << FILE_BITS)`.
- `isOutput()` проверяет бит `FILE_BITS` (11-й).

### `TaggedLocation<FileIdx>` — tagged-позиция

Содержит `RangeType` (диапазон `{begin, end}` с `point()` и `is_point()`), операторы сравнения с `uint32_t` (через offset) и между собой (только одного тега), арифметические операторы, конструкторы (invalid, от packed, от FileIdx+offset), методы `isValid()`, `isOutput()`, `fileIdx()`, `inc()`, `dec()`, `packedValue()`.

- `offset()` — **protected**, доступен только `Mapper<FileIdx>`, `Context` и друзьям. Внешний код использует операторы сравнения с числами, `fileIdx()`, `packedValue()`, `line_column()`.

### `Mapper<FileIdx>` — шаблонный базовый класс с данными маппинга

Содержит:
- Вложенные типы: `RangeMap` (from/to Range), `NameMap` (RangeMap + trustName + cppName)
- Данные маппинга: `m_forward`, `m_backward`, `m_nameMappings`, `m_cppToTrustName`
- Файловая информация: `m_inputs`, `m_outputs` (векторы `FileEntry`)
- Методы доступа к файлам: `get_input(raw)`, `get_output(raw)` — принимают `uint32_t raw`, едины для обоих пространств
- `filename(FileIdx)`, `source(FileIdx)` — работа с именами файлов (не зависит от пространства)
- `line_column(Location)`, `line(Location)`, `column(Location)`, `loc_from_line(FileIdx, line)` — работают с Location, дают compile-time проверку пространства
- `getFileHash()`, `assignMappingData()`
- `input_count()`, `output_count()`, `file_count()`
- LRU-кэши: `m_lc_cache` (offset → line:column), `m_loc_cache` (line → location)

### `Context` — наследует `Mapper<FileIdx>` (builder space)

- Создание и валидация позиций: `makeLoc()`, `makeRange()`, `validateLoc()`
- Добавление маппингов: `addRangeMapping()`, `addNameMapping()`, `StmtBegin()`, `StmtEnd()`
- Стековое создание маппинга: `mapStart()` (сохраняет позицию выходного файла в стеке, возвращает ключ) и `mapStop()` (завершает отображение по ключу)
- Извлечение текста: `sourceText()`, `outputBodyText()`
- Работа с выходными файлами: `add_output()`, `output_append()`, `output_prepend()`, `output_result()`, `output_body()`
- Финализация: `toReader()` → `SourceMapReader`, `reader()`
- Сериализация: `packMapping()`, `unpackMapping()`
- Диагностика: `report()` с форматированием через `std::format`

### `SourceMapReader` — наследует `Mapper<ReaderFileIdx>` (reader space)

- Factory: `fromMsgpack(data, size)`
- Поиск маппингов: `getMapTrustToCpp()`, `getMapCppToTrust()`, `getTrustFileMappings()`, `findRangesByLine()`
- Поиск имён: `getCppName()`, `getTrustName()`
- Сериализация: `packRanges()`, `packNames()`, `packToMsgpack()`
- `setFiles()`
- Приватные методы: `unpackRanges()`, `unpackNames()`, `findRange()`

### `DiagnosticEngine<FileIdx>` — шаблонный движок диагностики

- `report(Range, Severity, msg)` — вывод диагностики с привязкой к позиции
- `Context` использует `DiagnosticEngine<FileIdx>` (builder space)
- `Mapper` не использует диагностику (read-only)

### `FileEntry`

Единая структура для входных и выходных файлов, не зависит от пространства.

### Финализация выходных файлов в `toReader()`

`Context::toReader()` создаёт `SourceMapReader`:
1. Для каждого выходного файла берётся `body` и prepend из `m_outputBuffers`.
2. Все offset'ы в cpp-диапазонах пересчитываются: к каждому offset'у добавляется размер prepend.
3. `FileEntry.source` устанавливается в `prepend + body`.
4. Вычисляется хеш от `prepend + body`.
5. `SourceMapReader` создаётся с копией маппингов (с пересчитанными offset'ами) и метаданных.

После `toReader()` все данные в reader — в reader space. `Context` продолжает работать в builder space.

### Защита на уровне типов

Компилятор запрещает передачу Location из builder space в reader space API и наоборот. Для сравнения позиций из разных пространств используются `line()`/`column()`.

### Остальные компоненты

- **`OutputBuffer`** — хранит prepend-данные.
- **`MapStartEntry`** — запись в стеке `m_mapStack` для `mapStart`/`mapStop`: содержит `inputRange` (ключ) и `outputBegin` (сохранённая позиция в выходном файле).
- **`Options`** — система именованных опций.
- **`MsgpackWriter` / `MsgpackReader`** — сериализация/десериализация в msgpack.

### Юнит-тесты

Юнит-тесты для модуля `diag` находятся в `test/unit/diag/context_test.cpp`, `test/unit/diag/mapping_test.cpp` и `test/unit/diag/mapping_extended_test.cpp`. Тесты покрывают tagged-типы:
- Все тесты для `Context` используют `TaggedLocation<FileIdx>` (builder space).
- Все тесты для `SourceMapReader` используют `TaggedLocation<ReaderFileIdx>` (reader space).
- Сравнение offset'ов между разными пространствами запрещено на уровне типов.
- Для сравнения позиций из разных пространств используются `line()`/`column()` или явное преобразование.