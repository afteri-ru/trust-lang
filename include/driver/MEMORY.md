# MEMORY.md

> scope: include/driver
> role: persistent-memory
> last_reviewed: 2026-09-16
> review_period: 30
> max_size: 8000

## Architecture

Драйверный слой опций: модель разобранных опций (`driver/options.hpp`: `EmitFlags`, `PipelineSteps`,
`CompileMode`, `RuntimeLink`, `PipelineOpts`, `ParseResult`) + arity-aware CLI-инфраструктура
(`driver/cli.hpp`: `CliCategory`/`CliOpt`/`DriverOption`/`parseDriverArgs`/`driverHelp`/
`commonAnalysisOptions`/`extractServerCommand`) + единая точка применения опций анализа
(`driver/analysis_options.hpp`). Вынесен из `pipeline_lib`, чтобы бинарники и LSP-слой опций
использовали его без линковки всего конвейера.

## Facts and invariants

- **Единый CLI: arity-aware `parseDriverArgs` (DriverOption).** Опции драйверов (trust/trust-lsp/
  trust-dap/playground) — `enum + vector<DriverOption> + switch`; ест ровно заявленную арность;
  повторяемые `-l/-L` не «доедают» входной файл; `-W` применяются позже (единая точка `applyDiagnostics`).
  Двухсправочная модель: `--help` (`driverHelp`) vs `-Whelp` (`Options::printHelp`).
- **Общие опции анализа** (`--solver-mode`, `--keywords`, `-fsolver-loop-unroll`, `--stack-check*`) —
  ОДИН раз (`commonAnalysisOptions`); trust-lsp их не объявляет — `analysis_passthrough` отдаёт
  неизвестные `--name=value`/`-fname` в `applyAnalysisArgs`.
- **boolean-поведенческие флаги валидируются единообразно** (`parseBoolFlagValue`): `""/"on"`→true,
  `"off"`→false, иное — ошибка (без тихого fallback). Единая точка — `applyOption` (CLI) и
  валидаторы реестра флагов (`@__OPTION__`).
- **⚠ Ловушка (`argv[0]`):** `parseDriverArgs` ожидает полный argv (элемент [0] — имя программы);
  `applyAnalysisArgs` подставляет фиктивный argv[0], иначе первая опция списка «съедалась» бы как имя.
- **Позиционные ПОСЛЕ входа** допустимы только при `--run` (аргументы программы); без `--run` лишний
  позиционный — ошибка.
- **`codegen_args` в `PipelineOpts`** — нормализованный список ФАКТИЧЕСКИ заданных codegen-релевантных
  опций (вход кеша `--run`; логика кеша — в `pipeline`); нерелевантные (`-v/-q/-W/--temp-dir`) туда
  не попадают.

## Decisions

- Модель опций и CLI вынесены в `driver_lib`: LSP/playground/formatter получают опции, не линкуя
  `pipeline_lib`. `pipeline/pipeline.hpp` включает `driver/options.hpp`.
- Каждый драйверный бинарник объявляет СВОЮ `DriverOption`-таблицу; общая инфраструктура — одна.

## Relations

- `driver/analysis_options.hpp` зависит от реестров флагов `semantic/diag.hpp` и `transpiler/diag.hpp`
  (идентификаторы/метаданные флагов — заголовочно; применение — через `diag::Options`).
- Логика конвейера/кеша/режимов — `include/pipeline`.
