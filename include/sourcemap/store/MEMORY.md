# MEMORY.md

> scope: include/sourcemap/store
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Хранение исходников/вывода: `file_entry.hpp` (`FileEntry`), `source_map.hpp` (карта), `output_buffer.hpp`
(prepend-буфер вывода `Context::output_prepend`).

## Facts and invariants

- **⚠ `m_inputs/m_outputs` — `std::deque<FileEntry>`:** адреса `FileEntry` и их строковых буферов
  стабильны при росте. На буферы ссылаются `string_view` в Term/AST, поэтому входные файлы после
  `add_source` неизменяемы.
- **`OutputBuffer`:** префиксы группируются по namespace, дубли подавляются.
- **⚠ 1-based vs 0-based:** source map — 1-based (первый символ = offset 1), лексер внутри — 0-based;
  при создании диапазона токена обязателен `+1`. Синтаксические ошибки bison — 0-based.

## Relations

- `sourcemap/serialize`, `sourcemap/mapper`, `session` (`Context`), `diag`.
