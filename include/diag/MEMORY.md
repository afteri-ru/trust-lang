# MEMORY.md

> scope: include/diag
> role: persistent-memory
> last_reviewed: 2026-09-16
> review_period: 30
> max_size: 8100

## Architecture

Чистый слой диагностик и опций: `DiagnosticEngine` (вывод/подсчёт/форматирование), `Options` (реестр
severity-диагностик и feature-флагов, ключ — cli-имя), `Severity`, механизм
`TRUST_DIAG_SET`/`TRUST_FLAG_SET`. Source-маппинг и хранение исходников — в `sourcemap`; фасад
`Context` — в `session`.

## Facts and invariants

- **Options (diag) — единая точка применения `-W`:** все потребители идут через
  `applyDiagnostics → Options::parse_argv`. scoped push/pop, агрегаты `-Wall/-Wextra`, `-Werror`.
- **Команды справки `-Whelp*` — тема, а не флаг диагностики:** `-Whelp`/`-Whelp-dsl`/
  `-Whelp-predef-macros` выставляют `Options::help_topic_` (`helpRequested()`=тема!=None). Печать по теме
  — в приложении (trust.cpp): `Diagnostics`→`printHelp`; `DslMacros`/`PredefMacros`→макро-справка (diag не
  знает DSL/макросы; данные — из `Context::macroDefs()` и x-macro). Сентинела `All` нет.
- **Пер-компонентные id (без единого DiagId):** каждая компонента объявляет СВОЙ enum через
  `TRUST_DIAG_SET`/`TRUST_FLAG_SET` в своём заголовке. Метаданные — через ADL => diag остаётся листом.
- **Группы-агрегаты (clang-стиль):** центральный X-macro `WARN_GROUPS`; группа обрабатывается ТОЛЬКО
  при отсутствии `=value` (т.к. `deprecated` — и группа, и диагностика).
- **push/pop (ловушка):** дельты хранят СТАБИЛЬНОЕ (литерал) имя записи, НЕ переданный `string_view`
  (временный → висячая ссылка).
- `OutputBuffer` (prepend-буфер выходного файла) живёт в `sourcemap` (`Context::output_prepend`).
- Система группировки опций/флагов — `include/diag/OPTIONS.md`.

## Decisions

- Хранение исходников/маппинг вынесены в `sourcemap_lib`, фасад `Context` — в `session_lib`, чтобы
  чистые потребители диагностик/опций (напр. `types`) не тянули msgpack/zstd/elf.

## Relations

- `sourcemap` зависит от `diag` (severity); `session` — от `diag`, `sourcemap`, `attrs`.
