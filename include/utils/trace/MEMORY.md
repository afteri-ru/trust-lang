# MEMORY.md

> scope: include/utils/trace (+ src/utils)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 4000

## Architecture

Ядро отладочного вывода: `trace.hpp` (`TRUST_DEBUG`/`TRUST_SCOPE`, `Site`, `setFilter`) и
`trace_options.hpp` — реестр именованных опций дампа (x-macro `TRUST_SCOPE_DUMP_OPTIONS`).

## Facts and invariants

- **Гейт `TRUST_TRACE_ENABLED`** (CMake `TRUST_ENABLE_TRACE`: dev ON / release OFF): при 0 макросы
  вырезаны (аргументы не вычисляются, вызовы — no-op). Дефолт в заголовке — 0.
- **⚠ `setFilter`/`@__DEBUG__` управляет ТОЛЬКО сообщениями `TRUST_DEBUG`;** `@__DEBUG_SCOPE__` от
  фильтра НЕ зависит. Пустой фильтр → сообщения молчат. Первый аргумент макросов — строковый литерал.
- **Авто-токены `Site` из `__FILE__`** (разбор один раз на call-site): маски `*`/`?`, списки через `,`;
  абсолютный путь не хранится (детерминизм при разной точке сборки). **Компонент — ПЕРВЫЙ сегмент под
  `src/`/`include/`**, а не каталог internal-подкаталога (`src/semantic/core/x.cpp` → `component=semantic`,
  `rel=semantic/core/x.cpp`); для `test/`/`examples/`/голых путей — последние два сегмента.
- **Префикс на каждой строке вывода** (`prefixEachLine`/`shortenedPath`); `@__DEBUG__` печатает эхо
  фильтра.
- **⚠ Реестр опций дампа — `utils/trace/trace_options.hpp`:** ЕДИНЫЙ источник для парсера и семантики;
  новый ключ добавляется ТОЛЬКО там.

## Decisions

## Relations

- `syntax` (захват `@__DEBUG__`/`@__DEBUG_SCOPE__`), `semantic` (дамп состояния), корневой `MEMORY.md`.
