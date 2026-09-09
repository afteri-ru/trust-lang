# Driver - модель опций и CLI-инфраструктура

## Назначение

Общая для всех драйверных бинарников (trust, trust-lsp, trust-dap, playground) инфраструктура
командной строки и модель разобранных опций.

## Состав

- **`driver/options.hpp`** - модель опций: `EmitFlags` (что выводить), `PipelineSteps` (шаги
  конвейера), `CompileMode`, `RuntimeLink`, `PipelineOpts` (все разобранные опции) и `ParseResult`
  (опции + позиционные + собранные `-W`-аргументы + код возврата).
- **`driver/cli.hpp`** - arity-aware CLI: `CliCategory`/`CliOpt`/`DriverOption`, общий
  `parseDriverArgs`, генерация `driverHelp`, `commonAnalysisOptions` (единый источник общих опций
  анализа), `extractServerCommand`, `applyDiagnostics` (единая точка применения `-W`).
- **`driver/analysis_options.hpp`** - единая точка применения опций АНАЛИЗА к `diag::Options`:
  `applyBehavioralFlags`/`applyAnalysisOptions`/`applyAnalysisArgs`. Опции разбираются тем же
  arity-aware парсером, поэтому определения не дублируются.

## Принципы

- Опции драйвера (`DriverOption`) и диагностики (`diag::Options`) - ДВЕ разные таблицы; связывает их
  только инфраструктура.
- Arity-aware парсер: каждая опция объявляет точную арность (`Flag`/`Value`/`ValueList`/
  `OptionalValue`), позиционные аргументы собираются отдельно и не «доедаются» опциями-списками.
- Двухсправочная модель: `--help` (опции драйвера) vs `-Whelp` (диагностики).
- Каждый драйверный бинарник объявляет СВОЮ таблицу `DriverOption`; общие опции анализа доводятся
  через `analysis_passthrough` и `applyAnalysisArgs`.
