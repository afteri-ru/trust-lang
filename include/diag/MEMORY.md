# MEMORY.md

> scope: include/diag
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 12000

# Diag - архитектура

## Назначение

Модуль `diag` предоставляет инфраструктуру для:

- хранения исходного кода (входные `.src` файлы) и сгенерированного вывода (выходные `.cppt` / `.hppt` файлы);
- диагностики (ошибки, предупреждения, заметки) с привязкой к диапазонам позиций;
- маппинга позиций между входными и выходными файлами для использования при отладке.

## Ключевое архитектурное решение: tagged-пространства

Существуют **два пространства** для позиций:

1. **Builder space** - `Context` (до `toReader()`): offset относительно **body** выходного файла.
2. **Reader space** - `SourceMapReader` (после `toReader()`): offset относительно **full content** (prepend + body).

Offset'ы имеют разный смысл (body-relative vs full-content-relative), хотя `packed`-представление идентично. Смешение пространств - частая ошибка.

**Решение**: все типы позиций параметризованы тегом пространства:

- `TaggedFile<MapperFile>` / `TaggedFile<ReaderFile>` - идентификаторы файлов
- `TaggedLocation<MapperFile>` / `TaggedLocation<ReaderFile>` - позиции
- `SourceMap<MapperFile>` / `SourceMap<ReaderFile>` - данные маппинга

`Context` наследует `SourceMap<MapperFile>` (builder space). `SourceMapReader` наследует `SourceMap<ReaderFile>` (reader space). Компилятор запрещает передачу ReaderLocation в Context API и наоборот.

## Основные компоненты

### `TaggedFile<Tag>` - идентификатор файла

Упакован в `uint32_t raw` с битовым флагом входного/выходного файла. Ноль - невалидный. `MapperFile` и `ReaderFile` - два разных тега.

### `TaggedLocation<FileIdx>` - позиция в файле

Упакована в `uint32_t`: старшие биты - FileIdx, младшие - offset (1-based). Содержит вложенный `RangeType {begin, end}`. Операторы сравнения, арифметики, методы `inc()`/`dec()` работают с offset.

### `SourceMap<FileIdx>` - шаблонный базовый класс маппинга

Содержит файловую информацию (`m_inputs`, `m_outputs`), forward/backward маппинги, макрос-маппинги, name-маппинги, LRU-кэши line→column и loc_from_line.

### `Context` - builder space (наследует `SourceMap<MapperFile>`)

Создание файлов, позиций, диапазонов, маппингов. Стековый механизм `mapStart`/`mapStop`. Финализация через `toReader()`.

### `SourceMapReader` - reader space (наследует `SourceMap<ReaderFile>`)

Read-only. Десериализация из msgpack, поиск маппингов по позиции, поиск имён.

### `OutputBuffer` - буфер префиксов для выходного файла

Хранит набор уникальных строк-префиксов, сгруппированных по namespace (`std::map<std::string, std::set<std::string>> m_prefixes`).
`prepend(text, ns)` - добавляет строку целиком (без разбиения) для указанного namespace.
`build(indentSize = 4)` - собирает финальную строку:
- Глобальные префиксы (`ns == ""`) выводятся как есть, каждый на новой строке.
- Префиксы для конкретного namespace оборачиваются в `namespace ns { ... }` с отступом `indentSize`.
- Дубликаты в рамках одного namespace подавляются (`std::set`).
- Параметр `ns` по умолчанию `""` (глобальная область). Определён в `Context::output_prepend`.
- `Context::get_prepend` возвращает `std::string` по значению (без кеширования).

### `DiagnosticEngine` - движок диагностики

Принимает `MapperRange`, severity и сообщение. Аккумулирует диагностики для вывода.

### `FileEntry` - запись о файле

Хранит имя, содержимое, хеш. Вычисляет line/column через SparseCache.

## Поток данных

1. Builder: `Context::add_source()` / `add_output()` → создание файлов
2. Builder: `Context::makeRange()` / `makeLoc()` → создание позиций
3. Builder: `Context::addRangeMapping()` / `mapStart/mapStop` → маппинг
4. Финализация: `Context::toReader()` → пересчёт offset'ов (prepend) → `SourceMapReader`
5. Reader: `SourceMapReader::getMapTrustToCpp()` / `findCppToTrust()` → поиск

## Ограничения упаковки

Позиция упакована в `uint32_t`. Бит 31 - флаг выходного файла.

## Соглашение об offset'ах (input 1-based)

> ⚠ trap: source map - **1-based** (первый символ = offset 1); лексер внутри ведёт позиции **0-based**, поэтому при создании диапазона токена обязателен `+1` (`YY_MKRANGE`/`YY_TOKEN_SRC`). Синтаксические ошибки парсера (bison) отображают 0-based колонку - это отдельный механизм, не затрагивающий source map.

Диапазоны токенов в source map используют **1-based** offset'ы (первый символ файла - offset 1), единые для входных и выходных файлов. Именно под это рассчитаны `FileEntry::calc_column` (offset − 1 → 0-based), `SourceMap::getText`, `SourceMapWriter::mapStart`/`mapStop` (outputBegin = `size + 1`) и `locationToLspPosition`.

Лексер внутренне ведёт `m_current_pos`/`m_content_begin` в **0-based** (они используются и как индексы в исходный текст через `tokenText()`/`tokenStartOffset()`), поэтому при создании диапазона токена делается `+1` (`YY_MKRANGE`/`YY_TOKEN_SRC`). Синтаксические ошибки парсера используют отдельный bison-механизм локаций (не term-диапазоны) и отображают 0-based колонку - это не затрагивает source map.

**Входные файлы** (bit 31 = 0):
- файл: 9 бит (до 511 файлов), offset: 22 бита (~4MB на файл)
- `TaggedFile.raw = index + 1` (без флага)

**Выходные файлы** (bit 31 = 1):
- файл: 5 бит (до 31 файла), offset: 26 бит (~64MB на файл)
- `TaggedFile.raw = (index + 1) | OUTPUT_FILE_BIT`

`TaggedFile` и `TaggedLocation` используют независимые битовые раскладки (флаг в бите 31). Выход за пределы - FAULT.

**Константы:** см. `LocationPack` в `location.hpp`.

## Защита на уровне типов

Компилятор запрещает:
- Сравнение `ReaderLocation` с `MapperLocation` напрямую
- Передачу Reader-типов в Context API и наоборот

## Facts and invariants

- На компонент диагностики зависят transpiler и semantic (relation depends_on).

- Для кросс-пространственного сравнения - `line()`/`column()` или явное преобразование через `static_cast`