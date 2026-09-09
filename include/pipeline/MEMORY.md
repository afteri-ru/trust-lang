# MEMORY.md

> scope: include/pipeline
> role: persistent-memory
> last_reviewed: 2026-09-15
> review_period: 30
> max_size: 11900

## Architecture

Оркестратор компиляции: syntax Parser (Flex/Bison; DSL `@trust/dsl` через `ParseText`) →
`TermToAstConverter` (loader-free, НЕ мутирует Term) → semantic → transpile → сборка C++ через Makefile
(`kMakefileBuild` + `build.conf`, дефолты из CMake, `-std=c++23`). `runPipeline` при errorCount>0 после
ParseAST НЕ запускает semantic/transpile. Фиктивные in-memory источники помечаются в source map
префиксом `@`.

## Facts and invariants

- **Кеш `--run` учитывает codegen-опции.** Запись `.debug_trust_hash`: версия, строка `@opts\t<args>`,
  затем `файл\tmd5` главного файла и модулей. `<args>` — нормализованный список ФАКТИЧЕСКИ заданных
  codegen-релевантных опций (`PipelineOpts::codegen_args`, каноничная форма, сортировка + санитайз
  переводов строк). Нерелевантные (`-v/-q/-W/--temp-dir/-o`) кеш НЕ инвалидируют; значимые (напр.
  `-fno-overflow-check`) инвалидируют без смены исходников. Старые записи без `@opts` невалидны (одна
  пересборка).
- **`--run` и кеш:** бинарник запускается после сборки; кеш встраивается в ELF-секцию
  `.debug_trust_hash`, пути ОТНОСИТЕЛЬНЫЕ от CWD; чтение — `utils::elf::readElfSection` (dlopen для PIE
  не работает). Другой каталог с тем же именем файла — кеш НЕ применяется. Без `--temp-dir` выход —
  `<исходник>/.trust/<stem>/` (`--run` — `<cwd>/.trust/<stem>/`); не-`--run` кладёт артефакты ТОЛЬКО в
  `.trust`. Позиционные ПОСЛЕ входа при `--run` — аргументы программы (и через шебанг); без `--run`
  лишний позиционный — ошибка.
- **Шапка `.cppt` несёт `// trust-options: <args>`** (та же `codegenArgsRecord`): пишется ТОЛЬКО при
  непустых фактически заданных codegen-опциях и входит в `prefix` (учитывается source-map). Это
  трассируемость, НЕ механизм кеша.
- **Однофайловый режим `-fsingle-file`/`-fno-single-file`:** обёртка `int main` встраивается в тот же
  `.cppt` модуля; отдельный `_main.cppt`/`SRC_MAIN` не создаётся. По умолчанию включён при `--run`.
  «Модуль-скрипт» (top-level код без `__main__`) — python-подобно: top-level операторы (вкл. `x := ...`)
  DeclEmitter оборачивает в синтезируемую entry `<модуль>__main__` (переменные скрипта — локальные
  main); функции/типы/области/`{% %}`-embed остаются на namespace. Entry может задаваться C++-embed,
  поэтому при `!sawEntry` он проверяется по тексту `.cppt`.
- **Скачиваемый архив песочницы (`emitBuildDirArchive`, trust-lsp `--emit-build-dir`) — однофайловый**
  (`opts.single_file = true`, тот же путь, что `--run`): скрипт без `__main__` компилируется.
  Многофайловый путь (`-o` без `-fsingle-file`) скрипт без main НЕ поддерживает.
- **CLI и модель опций вынесены в `driver`** (`driver/options.hpp`, `driver/cli.hpp`,
  `driver/analysis_options.hpp`): arity-aware `parseDriverArgs`/`DriverOption`, единый
  `commonAnalysisOptions`, единая точка применения `-W` (`applyDiagnostics`), `parseBoolFlagValue`.
  Подробности — `include/driver/MEMORY.md`.
- **Макросы:** `trust/dsl.src` загружается один раз в `Context` (`setMacro`); каждый Parser наследует
  Macro через `Context&`. `--dsl <file>` заменяет, `--no-dsl` отключает.
- **LSP-режим:** `allow_semantic_on_errors` (LSP включает всегда) разрешает семантику на частичном AST;
  Transpile при ошибках не выполняется. `releaseTypes()` отдаёт владение TypeRegistry вызывающему
  (чтобы TypeId в SymbolIndex оставался валидным).
- **Декомпозиция:** `pipeline.cpp` разбит на модульные TU (только перестановка кода); `kEmbeddedDslSrc`
  (#embed dsl.src) остаётся в `pipeline.cpp`. `-Wno-c23-extensions` нужен файлам с #embed.

## Decisions

## Relations
