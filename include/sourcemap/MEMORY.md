# MEMORY.md

> scope: include/sourcemap
> role: persistent-memory
> last_reviewed: 2026-11-06
> review_period: 30
> max_size: 4000

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
- `TaggedFile`/`TaggedLocation` — НЕЗАВИСИМЫЕ битовые раскладки (бит 31 — флаг; вход и выход
  различаются разрядностью).
- `OutputBuffer` (`Context::output_prepend`): префиксы группируются по namespace, дубли подавляются.
- `SourceMapReader` загружается из embedded ELF-секции `.debug_trust_map` (`fromElf()`); внешние
  `.map`/`.trust` не используются. msgpack-типы нужны только reader-стороне: writer объявляет reader
  вперёд и не включает msgpack.
- **⚠ Шапка .cppt детерминирована (только ДАТА, не время):** `output_prepend_leading` берёт
  `utils::currentDate()` (не `currentTimestamp()`). Иначе MD5 выходного файла в `.src_map` менялся
  бы каждый прогон, и повторные сборки (`ccache` у тулчейна) не давали бы хитов.

## Decisions

- Source map выделен из `diag`: чистые диагностики/опции не зависят от хранения исходников и msgpack.

## Relations

- Зависит от `diag` (severity) и `utils` (cache/file_io/elf/zstd) + msgpack/LLVM MD5.
- Владелец `SourceMapWriter` — фасад `Context` (`session`).
