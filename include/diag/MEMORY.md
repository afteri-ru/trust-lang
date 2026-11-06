# MEMORY.md

> scope: include/diag
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Чистый слой диагностик и опций: `DiagnosticEngine` (вывод/подсчёт), `Options` (реестр severity-диагностик и
feature-флагов, ключ — cli-имя), `Severity`, механизм `TRUST_DIAG_SET`/`TRUST_FLAG_SET`. Source-маппинг —
`sourcemap`, фасад `Context` — `session`. Группировка опций — `diag/OPTIONS.md`.

Рендеринг — сменный форматтер (`diag/diag_formatter.hpp` + `diag/serialize/`): `TextFormatter` (по умолч.),
`JsonFormatter`, `SarifFormatter` — проекции ЕДИНОЙ записи `DiagnosticEntry`; движок фильтрует/считает. Выбор —
`--diagnostics-format=<text|json|sarif>`. Локацию для всех рендеров и LSP даёт ЕДИНЫЙ
`diag/diagnostic_location.hpp`. Детали реализации — internal-подкаталоги `diag/engine/` (движок и декодер
локации), `diag/options/` (реестр опций), `diag/registry/` (реестр базовых диагностик), `diag/decl/`
(объявления опций) и `diag/serialize/` (форматтеры).

## Facts and invariants

- **Единая точка отчёта — `diag::reportWithOptions`** (severity из `Options`, Ignore подавляет вывод); её же
  используют компоненты без `session`. Второй копии разбора severity нет.
- **⚠ `DiagnosticEngine::m_diagnostics` — `std::deque`:** `report()` возвращает `DiagnosticEntry*`, к которому
  затем прикрепляется `fixit`; добавление Note-кадров контекста не должно инвалидировать указатель. `vector`
  здесь запрещён (реаллокация → use-after-free).
- **`duplicate-import`** — диагностика импорта/связывания (default Error); загрузка файла идемпотентна и не
  диагностируется.
- **Лимит ошибок — свойство `DiagnosticEngine`** (default 20, 0 = без лимита): после N-й ошибки ОДНО
  внутреннее Note «too many errors emitted» в обход severity/minSeverity, далее все не-Fatal подавлены.
  `clear()` сбрасывает счётчик, лимит сохраняется.
- **Options — единая точка применения `-W`:** все потребители — через `applyDiagnostics → parse_argv`
  (scoped push/pop, агрегаты `-Wall/-Wextra`, `-Werror`).
- **Значения поведенческих опций — `diag/flag_values.hpp`:** `parseBoolFlagValue` (`""`/`on`/`off`) и
  `parseSizeValue` (неотрицательное целое; знак/нецифры/переполнение — ошибка, тихого fallback нет).
- **⚠ `-Whelp*` — тема справки, а не флаг диагностики:** выставляют `Options::help_topic_`, печать — в
  приложении (`trust.cpp`), т.к. `diag` не знает DSL/макросы. Сентинела `All` нет.
- **Пер-компонентные DiagId (без единого DiagId):** каждая компонента объявляет СВОЙ enum через
  `TRUST_DIAG_SET`/`TRUST_FLAG_SET`; метаданные — через ADL, поэтому `diag` остаётся листом.
- **Группы-агрегаты (clang-стиль):** центральный X-macro `WARN_GROUPS`; группа — только без `=value`
  (`deprecated` — и группа, и диагностика).
- **⚠ push/pop (ловушка):** дельты хранят СТАБИЛЬНОЕ (литеральное) имя записи, а не переданный `string_view` —
  иначе висячая ссылка.
- `OutputBuffer` (prepend-буфер) живёт в `sourcemap` (`Context::output_prepend`).
- **⚠ Текст исходной строки — через `diag::SourceTextProvider`:** реализация — `session` (`Context`), поэтому
  ребра `diag->session` нет.
- **⚠ Рендеринг — сменный `DiagnosticFormatter`, владение у движка:** движок всегда владеет text-форматтером;
  json/sarif — проекции той же `DiagnosticEntry`, второй модели данных нет. Границы JSON/SARIF открывает
  конструктор и закрывает деструктор форматера. Невалидное значение формата — ошибка CLI, не тихий fallback.
- **Единый декодер локации — `diagnosticLocation`/`decodeRange`:** рендеры и LSP резолвят file/line/col через
  единый `SourceTextProvider`. **Ловушка offset==0:** позиция «до начала файла» (модульный импорт/шапка)
  нерезолвима — декодер приравнивает её к отсутствующей (`valid()==false`); иначе форматтер/LSP бросали бы
  EXPECT.
- **cli-имена поведенческих флагов — `diag/flag_ids.hpp`:** владелец берёт имя оттуда, `driver` применяет флаг
  по имени, не включая заголовки владельцев.
- **⚠ Стек контекста диагностики — у ДВИЖКА (`DiagnosticEngine::ContextScope`/`pushContext`/`popContext`, R30):**
  кадры (диапазон + текст) снимаются RAII (в т.ч. по исключению); ошибка/предупреждение, выданное внутри
  стека, сопровождается Note-записями от внутреннего кадра к внешнему (единый путь рендеринга, R31).
  Диагностическое состояние контекста НЕ хранится в AST/узле (R30).
- **⚠ Цепочка раскрытия макроса — те же Note-записи единой модели (R50), порождённые в `output()`:**
  источник — порт `SourceTextProvider::expansionChain(range)` (владелец пары — `sourcemap`); отдельного
  поля provenance и структурного id связи НЕТ (идентичность — локация записи + карта). Имя кадра — слово
  текстового буфера. Порядок: динамические кадры (стек) → макро-кадры; защита — `m_emittingChain`.

## Decisions

- Хранение исходников/маппинг вынесены в `sourcemap_lib`, фасад `Context` — в `session_lib` (чистые
  потребители не тянут msgpack/zstd/elf).

## Relations

- `sourcemap` зависит от `diag` (severity); `session` — от `diag`, `sourcemap`, `attrs`.
