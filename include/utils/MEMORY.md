# MEMORY.md

> scope: include/utils
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Общие утилиты. Публичный API — корень: `utils.hpp` (umbrella + `resolveTempDir`/`executablePath`),
`error.hpp` (`FAULT`/`EXPECT`), `hash.hpp` (MD5), `build_record.hpp` (формат записи кеша `--run` +
раскладка build-каталога), `string_interner.hpp` (пул строк),
`import_masks.hpp`, `partial_switch.hpp`, `string_view_hash.hpp`, `strings.hpp` (umbrella строковых
утилит). Другие компоненты включают ТОЛЬКО эти заголовки.

Детали реализации — в internal-подкаталогах:

| каталог | MEMORY | ответственность |
|---|---|---|
| `utils/io/` | `io/MEMORY.md` | `io`/`file_io`/`socket_io`, транспортный фрейминг, URI, zstd |
| `utils/proc/` | `proc/MEMORY.md` | ELF-секции, backtrace, кеш, время |
| `utils/digest/` | — (инварианты в коде) | хеширование содержимого (MD5: `md5Hex`/`md5HexFile`) |
| `utils/strings/` | `strings/MEMORY.md` | `strings_text/ident/names/embed/escape/json/utf8` |
| `utils/trace/` | `trace/MEMORY.md` | отладочная трассировка и реестр опций дампа |

## Facts and invariants

- **⚠ `StringInterner` (пул строк):** append-only, адреса стабильны (`deque` + `unordered_set`);
  `internText()` пишет в АКТИВНЫЙ пул (`StringInternerScope`, thread-local). Вне компиляции —
  только по явному `ManualInternerScope` (процессный пул), иначе `EXPECT`: молчаливого fallback в
  процессный пул нет. Пул компиляции — per-Context без mutex; процессный — с mutex. Удалять записи
  НЕЛЬЗЯ (view инвалидируются); `owns()` — проверка владения.
- **`qualifyTrustName` живёт здесь, а не в `types/names.hpp`:** домен trust-имени нужен слоям
  НИЖЕ `types` (`ast`), и он обязан оставаться в одной точке.
- **⚠ Предикаты имён — ОДИН владелец `utils/strings.hpp`:** `isStaticName`/`isMacroName`/
  `isLocalName`/`isLocalAnyName`/`isTrustReservedName`/`isQualifiedName`; `IdentName`
  делегирует им, локальные `find("::")` в потребителях запрещены.
- **⚠ Прозрачный хеш строки — ОДИН `utils/string_view_hash.hpp`:** второй экземпляр (в `detail`
  интернера или локально) запрещён; ключи `unordered_map/set` по `string_view` используют его.
- **⚠ Путь в записи кеша — ОДИН `utils::cachePathOf`:** правило «относительно CWD» принадлежит формату
  `build_record.hpp`, writer (`pipeline`) и reader (`toolchain`) применяют его, а не свои копии.

## Relations

- Связь с языком: `syntax` (захват `@__DEBUG__`/`@__DEBUG_SCOPE__`) и `semantic` (дамп состояния).
- Отладочная диагностика целиком описана в корневом `MEMORY.md` и `utils/README.md`.
