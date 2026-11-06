# MEMORY.md

> scope: include/sourcemap
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

Хранение исходников (`.src`) и вывода (`.cppt`/`.hppt`), диагностики по диапазонам, маппинг вход↔выход,
msgpack-сериализация. Детали формата и раскладки заголовков — `sourcemap/README.md`; реализация — internal-подкаталоги `store/`, `serialize/`.

## Facts and invariants

- **⚠ Tagged-пространства позиций:** `Context` (builder, offset от body) и `SourceMapReader` (reader,
  offset от полного содержимого = prepend+body). `packed`-представление идентично, поэтому смешение
  скрыто; типы параметризованы тегом и компилятор запрещает кросс-передачу.
- **⚠ `m_inputs/m_outputs` — `std::deque<FileEntry>`:** адреса `FileEntry` и их строковых буферов
  стабильны при росте. На буферы ссылаются `string_view` в Term/AST, поэтому входные файлы после
  `add_source` неизменяемы.
- **⚠ 1-based vs 0-based (ловушка):** source map — 1-based (первый символ = offset 1), лексер внутри —
  0-based, поэтому при создании диапазона токена обязателен `+1`. Синтаксические ошибки bison —
  0-based (отдельный механизм). Под 1-based рассчитаны `calc_column`, `mapStart/mapStop`,
  `locationToLspPosition`.
- **⚠ Диапазон и имя — разные механизмы:** name-mapping (`addNameMapping`) — список 1:N и НЕ ключуется
  диапазоном; range-mapping (`mapStart`/`mapStop`) ключуется началом trust-диапазона 1:1 и падает `FAULT`
  при повторе. `MapStartEntry::recordRange=false` создаёт запись только для позиции имени: диапазон в карту
  не попадает (используется, когда range-mapping объявления принадлежит другому артефакту — R40).
  `save_output` при активном `suppressMapping()` — `EXPECT`-нарушение (артефакт без карты запрещён).
- **⚠ Пара раскрытия макроса — `m_macroForward` (input→input, `use → definition`), ключ — начало `use`:** читается
  `macroDefinitionAt` и цепочкой `macroExpansionChain` (единственный поиск — `store/macro_lookup.hpp`,
  общий с reader). **Ловушка:** вложенный вызов переиспользует диапазон внешнего (`call_range`), ключи
  совпадают и запись внутреннего раскрытия не создаётся → цепочка одноуровневая. Имя раскрытия не хранится:
  слово текста по точке использования (`store/word_at.hpp`).
- `TaggedFile`/`TaggedLocation` — НЕЗАВИСИМЫЕ битовые раскладки (бит 31 — флаг; вход и выход
  различаются разрядностью).
- `OutputBuffer` (`Context::output_prepend`): префиксы группируются по namespace, дубли подавляются.
- `SourceMapReader` загружается из embedded ELF-секции `.debug_trust_map` (`fromElf()`); внешние
  `.map`/`.trust` не используются. Встраивание — байтовый массив в генерируемом `.cppt`
  (стандартный C++: артефакт собирается clang и gcc); msgpack-типы нужны только reader-стороне:
  writer объявляет reader вперёд и не включает msgpack.
- **⚠ Шапка генерируемых артефактов детерминирована (только ДАТА, не время):** шапки
  `.cppt`/`.src_map`/`build.conf`/пакета берут `utils::currentDate()` — единый помощник
  (`utils/proc/time.hpp`); варианта с секундной точностью в проекте нет. Иначе MD5 выходного
  файла (`.src_map`/`build.conf`) менялся бы каждый прогон, ломая R42 и ccache-хиты.

## Decisions

- Source map выделен из `diag`: чистые диагностики/опции не зависят от хранения исходников и msgpack.

## Relations

- Зависит от `diag` (severity) и `utils` (cache/file_io/elf/zstd) + msgpack/LLVM MD5.
- Владелец `SourceMapWriter` — фасад `Context` (`session`).
