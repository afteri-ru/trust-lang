# MEMORY.md

> scope: include/utils
> role: persistent-memory
> last_reviewed: 2026-09-23
> review_period: 30
> max_size: 4100

## Architecture

`utils/trace.hpp` — ядро отладочного вывода КОМПИЛЯТОРА (уровень 2: источник данных):
`TRUST_DEBUG` (сообщения) и `TRUST_SCOPE` (дамп). Полное описание и порядок использования — в
корневом `MEMORY.md` («Отладочная диагностика анализатора»).

## Facts and invariants

- **Гейт `TRUST_TRACE_ENABLED`** (CMake `TRUST_ENABLE_TRACE`: dev ON / release OFF): при 0 макросы
  вырезаны (аргументы не вычисляются, вызовы — no-op). Дефолт в заголовке — 0.
- `setFilter`/`@__DEBUG__` управляет ТОЛЬКО сообщениями `TRUST_DEBUG`; `@__DEBUG_SCOPE__` от фильтра
  НЕ зависит. Пустой фильтр → сообщения молчат.
- **Авто-токены `Site` из `__FILE__`** (разбор один раз на call-site `static const Site`):
  `root|path|component|rel|stem|file` + явные теги; маски `*`/`?`, списки через `,`; абсолютный путь не
  хранится и не печатается (детерминизм при разной точке сборки).
- **Первый аргумент макросов — строковый литерал.**
- **Префикс на каждой строке вывода** (`utils::prefixEachLine`/`shortenedPath`): `.../file:line:` — место
  вызова для `TRUST_DEBUG`, локация макроса для `@__DEBUG*`; `@__DEBUG__` дополнительно печатает эхо
  применённого фильтра.
- **Реестр опций дампа — `utils/trace_options.hpp`** (x-macro `TRUST_SCOPE_DUMP_OPTIONS`):
  `level|types|max|count` (сентинел `Count`); значения без кавычек. Единый источник: валидация парсером
  (до AST, диагностика со списком) + применение семантикой.

## Decisions

## Relations

- Связь с языком: `include/syntax` (захват `@__DEBUG__`/`@__DEBUG_SCOPE__`) и `include/semantic`
  (дамп, `analyzeDebugStmt`).
