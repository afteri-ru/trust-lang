# MEMORY.md

> scope: include/utils/strings
> role: persistent-memory
> last_reviewed: 2026-11-04
> review_period: 30
> max_size: 3000

## Architecture

Строковые утилиты: `strings_text.hpp` (trim/tokenize/glob/filename/path), `strings_ident.hpp` (разбор
идентификаторов и сигналов), `strings_names.hpp` (trust↔C++ мангling), `strings_embed.hpp` (`#embed`),
`strings_escape.hpp` (escape/unescape), `strings_utf8.hpp` (UTF-8). Публичный зонтик — `utils/strings.hpp`.

## Facts and invariants

- **⚠ Публичный контракт — `utils/strings.hpp`;** заголовки этого каталога другие компоненты НЕ включают
  (`internal_boundary_guard`): даже `strings_text/ident/names` доступны снаружи только через фасад.

## Relations

- `utils/strings.hpp` (фасад), `syntax/macro.h`, `symbols/symbol_index.hpp` (потребители фасада).
