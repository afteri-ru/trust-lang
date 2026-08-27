# MEMORY.md

> scope: include/pipeline
> role: persistent-memory
> last_reviewed: 2026-08-25
> review_period: 30
> max_size: 6865

## Architecture

Pipeline — оркестратор компиляции: чтение источника → syntax Parser (Flex/Bison, единым вызовом;
встроенный DSL `"@trust/dsl"` — через `Parser::ParseText`) → конвертер `TermToAstConverter`
(ast_lib, loader-free, НЕ мутирует Term) → semantic → transpile → C++ сборка через Makefile.
Фиктивные (in-memory) источники помечаются в source map префиксом `@` — файла на диске нет.
`runPipeline` при `errorCount>0` после ParseAST НЕ запускает semantic/transpile.

Компиляция: pipeline генерирует рядом с `.cppt` `Makefile` (шаблон из `makefile_build.hpp`,
`kMakefileBuild`) и `build.conf` (опции компиляции). Параметры по умолчанию — из CMake (`-std=c++23`),
переопределяются через `build.conf`/env.

## Facts and invariants

- **`--run` и кеш:** после успешной сборки бинарник запускается. Кеш — запись «версия + список
  `файл\tmd5`» встраивается в ELF-секцию `.debug_trust_hash`; пути в записи ОТНОСИТЕЛЬНЫЕ от CWD.
  Проверка читает секцию через `utils::elf::readElfSection` (dlopen для PIE не работает), сверяет
  версию `TRUST_VERSION_FULL` и md5 файлов относительно CWD. Запись для другой программы с тем же
  именем файла из другого каталога кеш НЕ применяет. Без `--temp-dir` выходные файлы — в
  `./.trust/<stem>/` (подкаталог по имени файла, чтобы не перезаписывали Makefile/build.conf).
- **Единый CLI: arity-aware парсер (таблица DriverOption).** Опции драйверных бинарников (trust,
  trust-lsp, trust-dap) объявляются единообразно: `enum XxxOptId` + `vector<DriverOption>` +
  switch. Парсер `parseDriverArgs` (header-only, `cli.hpp`) потребляет ровно заявленную арность,
  повторяемые `-l`/`-L` не «доедают» входной файл. `-W<diagnostics>` применяются позже через
  `applyDiagnostics → Options::parse_argv` (единая точка). Категории `Diagnostics` нет — диагностики
  это отдельная таблица diag/Options (`-Whelp`), двухсправочная модель.
- **Общие опции анализа** (`--solver-mode`, `--keywords`, `-fsolver-loop-unroll`) определены ОДИН раз
  (`commonAnalysisOptions` в cli.hpp). trust-lsp НЕ объявляет их в своей таблице: общий parseDriverArgs
  при `analysis_passthrough` собирает неизвестные `--name=value`/`-fname` и отдаёт в `applyAnalysisArgs`
  (`analysis_options.hpp`) — набор общих опций может быть ЛЮБЫМ без доработки LSP.
- **Макросы:** встроенный `trust/dsl.src` загружается один раз в `Context` (`Context::setMacro`),
  каждый Parser наследует `Macro` через `Context&`. `--dsl <file>` заменяет, `--no-dsl` отключает.
- **LSP-режим:** `PipelineOpts::allow_semantic_on_errors` (LSP включает всегда) разрешает семантику
  на частичном AST; Transpile при ошибках не выполняется. `releaseTypes()` отдаёт владение
  `TypeRegistry` вызывающему (для LSP, чтобы `TypeId` в `SymbolIndex` оставался валидным).
- **Декомпозиция:** `pipeline.cpp` разбит на модульные TU (`io`, `runtime_locator`, `build`, `run`,
  `source_map`, `archive`, `module_info`), `pipeline.hpp` — «зонтик» (включает модульные заголовки).
  Инвариант: декомпозиция — только перестановка кода, `kEmbeddedDslSrc` (#embed dsl.src) остаётся в
  `pipeline.cpp`. `-Wno-c23-extensions` нужен и `pipeline.cpp`, и `source_map.cpp` (оба содержат #embed).
