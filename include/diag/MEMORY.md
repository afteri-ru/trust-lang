# MEMORY.md

> scope: include/diag
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 5000

## Architecture

Чистый слой диагностик и опций: `DiagnosticEngine` (вывод/подсчёт), `Options` (реестр
severity-диагностик и feature-флагов, ключ — cli-имя), `Severity`, механизм
`TRUST_DIAG_SET`/`TRUST_FLAG_SET`. Source-маппинг — `sourcemap`, фасад `Context` — `session`.
Группировка опций/флагов описана в `include/diag/OPTIONS.md`.

Детали реализации — internal-подкаталог `diag/decl/` (объявления опций); `diag/options.hpp`,
`diag/diag_set.hpp`, `diag/registry.hpp` — публичный API корня.

## Facts and invariants

- **Единая точка отчёта — `diag::reportWithOptions`** (severity из `Options`, Ignore подавляет вывод);
  `Context::report` и компоненты без `session` (напр. `types`) используют её. Второй копии разбора
  severity нет.
- **`duplicate-import`** — диагностика импорта/связывания (default Error, конфигурируемая); загрузка
  файла идемпотентна и не диагностируется.
- **Лимит ошибок — свойство `DiagnosticEngine`** (`kDefaultErrorLimit`=20, 0 = без лимита): после N-й
  ошибки ОДНО внутреннее Note-сообщение «too many errors emitted» в обход severity/minSeverity, затем
  все не-Fatal диагностики подавляются. `clear()` сбрасывает счётчик, значение лимита сохраняется.
- **Options (diag) — единая точка применения `-W`:** все потребители идут через
  `applyDiagnostics → Options::parse_argv` (scoped push/pop, агрегаты `-Wall/-Wextra`, `-Werror`).
- **Значения поведенческих опций — `diag/flag_values.hpp`:** `parseBoolFlagValue` (`""`/`on`/`off`) и
  `parseSizeValue` (неотрицательное целое; знак/нецифры/переполнение — ошибка, тихого fallback нет).
- **⚠ `-Whelp*` — тема справки, а не флаг диагностики:** выставляют `Options::help_topic_`, печать —
  в приложении (`trust.cpp`), т.к. `diag` не знает DSL/макросы. Сентинела `All` нет.
- **Пер-компонентные DiagId (без единого DiagId):** каждая компонента объявляет СВОЙ enum через
  `TRUST_DIAG_SET`/`TRUST_FLAG_SET`; метаданные — через ADL, поэтому `diag` остаётся листом.
- **Группы-агрегаты (clang-стиль):** центральный X-macro `WARN_GROUPS`; группа обрабатывается ТОЛЬКО
  без `=value` (`deprecated` — и группа, и диагностика).
- **⚠ push/pop (ловушка):** дельты хранят СТАБИЛЬНОЕ (литеральное) имя записи, а не переданный
  `string_view` — иначе висячая ссылка.
- `OutputBuffer` (prepend-буфер вывода) живёт в `sourcemap` (`Context::output_prepend`).
- **⚠ Текст исходной строки — через `diag::SourceTextProvider`:** `DiagnosticEngine` его получает
  (`setSourceTextProvider`), реализация — `session` (`Context`), поэтому ребра `diag->session` нет.
- **cli-имена поведенческих флагов — `diag/flag_ids.hpp`** (нейтральные константы): владелец берёт имя
  оттуда, `driver` применяет флаг по имени, не включая заголовки владельцев.

## Decisions

- Хранение исходников/маппинг вынесены в `sourcemap_lib`, фасад `Context` — в `session_lib`, чтобы
  чистые потребители диагностик/опций (напр. `types`) не тянули msgpack/zstd/elf.

## Relations

- `sourcemap` зависит от `diag` (severity); `session` — от `diag`, `sourcemap`, `attrs`.
