# MEMORY.md

> scope: include/driver
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Драйверный слой опций: модель разобранных опций (`driver/options.hpp`) + arity-aware CLI
(`driver/cli.hpp`) + единая точка применения опций анализа (`driver/analysis_options.hpp`) + нормализация
codegen-опций (`driver/codegen_args.hpp`, строка `@opts` кеша `--run` / шапки `.cppt`). Вынесен из
`pipeline_lib`, чтобы бинарники и LSP использовали опции без линковки конвейера. Логика конвейера/режимов —
`pipeline`, детали — `driver/README.md`.

## Facts and invariants

- **Единый CLI: arity-aware `parseDriverArgs`.** Каждый бинарник объявляет СВОЮ таблицу (enum +
  `vector<DriverOption>` + switch); парсер съедает ровно заявленную арность, поэтому повторяемые `-l/-L` не
  «доедают» входной файл. `-W` применяются позже (`applyDiagnostics`). Две справки: `--help` и `-Whelp`.
- **Общие опции анализа — ОДИН раз** (`commonAnalysisOptions`); `-ferror-limit` — в таблице `trust`.
  `trust-lsp` их не объявляет: `analysis_passthrough` отдаёт неизвестные `--name=value`/`-fname=value` в
  `applyAnalysisArgs`.
- **`-f<name>=<value>` — значение небулевой поведенческой опции:** `-f<name>`/`-fno-<name>` остаются булевыми;
  для не-Flag опции `=` обязателен (иначе ошибка).
- **Лимит ошибок** — поведенческая опция (`PipelineOpts::error_limit`, 0 = без лимита), НЕ severity.
- **`--diagnostics-format=<text|json|sarif>`** — поведенческая опция вывода (НЕ анализ): строка, валидация в
  `parseCommandLine`; применение — `trust.cpp` до первой диагностики. В кеш `--run` не входит.
- **boolean-флаги валидируются единообразно** (`parseBoolFlagValue`): `""`/`"on"`→true, `"off"`→false, иное —
  ошибка (тихого fallback нет). Точки: `applyOption` и валидаторы реестра (`@__OPTION__`).
- **⚠ Ловушка `argv[0]`:** `parseDriverArgs` ждёт полный argv ([0] — имя программы); `applyAnalysisArgs`
  подставляет фиктивный `argv[0]`, иначе первая опция «съедалась» бы как имя.
- **Позиционные ПОСЛЕ входа** допустимы только при `--run` (аргументы программы); иначе — ошибка.
- **`codegen_args` в `PipelineOpts`** — нормализованный список ФАКТИЧЕСКИ заданных codegen-релевантных опций
  (вход кеша `--run`); нерелевантные (`-v/-q/-W/--temp-dir`) не попадают.

## Decisions

- Модель опций и CLI вынесены в `driver_lib`: LSP/playground/formatter получают опции, не линкуя `pipeline_lib`.
  Общая инфраструктура парсинга — одна, таблицы — у каждого бинарника свои.

## Relations

- `driver/analysis_options.hpp` применяет флаги по cli-имени из нейтрального `diag/flag_ids.hpp` через
  name-API `diag::Options`; заголовки владельцев не включаются.
