# MEMORY.md

> scope: include/utils/strings
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Строковые утилиты: `strings_text.hpp` (trim/tokenize/glob/filename/path), `strings_ident.hpp` (разбор
идентификаторов и сигналов), `strings_names.hpp` (единый ОБРАТИМЫЙ кодек trust-имени в C++
идентификатор), `strings_embed.hpp` (языковые C++-вставки `{% ... %}` — не директива C++ `#embed`), `strings_escape.hpp` (escape/unescape),
`strings_json.hpp` (`escapeJson` — единое JSON-экранирование проекта), `strings_utf8.hpp` (UTF-8).
Публичный зонтик — `utils/strings.hpp`.

## Facts and invariants

- **⚠ Публичный контракт — `utils/strings.hpp`;** заголовки этого каталога другие компоненты НЕ включают
  (`internal_boundary_guard`): даже `strings_text/ident/names` доступны снаружи только через фасад.
- **⚠ C++-имя — ЕДИНЫЙ ОБРАТИМЫЙ кодек (`strings_names.hpp`):** `encodeIdent`/`decodeIdent`
  инъективны (`decodeIdent(encodeIdent(x)) == x`): безопасное ASCII (`[A-Za-z_][A-Za-z0-9_]*`, не
  ключевое слово) — тождественно; иначе — маркер `_u_` + `_HH_` на небезопасный байт. `nameToCpp`
  дополнительно срезает ведущий `$` (локальная) и разбирает `::` по сегментам. **Модуль в C++-имя
  НЕ входит** — повторять кодек по месту запрещено (иначе кодоген/embed/печать типов разойдутся).

## Relations

- `utils/strings.hpp` (фасад), `syntax/macro.h`, `symbols/symbol_index.hpp` (потребители фасада).
