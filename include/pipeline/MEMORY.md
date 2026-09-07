# MEMORY.md

> scope: include/pipeline
> role: persistent-memory
> last_reviewed: 2026-08-25
> review_period: 30
> max_size: 8238

## Architecture

Оркестратор компиляции: чтение → syntax Parser (Flex/Bison; DSL `@trust/dsl` — через `ParseText`) →
`TermToAstConverter` (loader-free, НЕ мутирует Term) → semantic → transpile → C++ сборка через Makefile.
Фиктивные (in-memory) источники помечаются в source map префиксом `@`. `runPipeline` при errorCount>0
после ParseAST НЕ запускает semantic/transpile. Рядом с `.cppt` генерируются `Makefile`
(`kMakefileBuild`) и `build.conf`; параметры по умолчанию — из CMake (`-std=c++23`).

## Facts and invariants

- **`--run` и кеш:** после сборки бинарник запускается. Кеш («версия + список `файл\tabmd5`») встраивается
  в ELF-секцию `.debug_trust_hash`; пути ОТНОСИТЕЛЬНЫЕ от CWD. Чтение — `utils::elf::readElfSection`
  (dlopen для PIE не работает). Другой каталог с тем же именем файла — кеш НЕ применяется. Без
  `--temp-dir` выходные файлы — `<исходник>/.trust/<stem>/` (`--run` — `<cwd>/.trust/<stem>/`); не-`--run`
  кладёт артефакты ТОЛЬКО в `.trust`. Позиционные ПОСЛЕ входного при `--run` — аргументы программы
  (и через шебанг `./prog.src a b`); без `--run` лишний позиционный — ошибка.
- **Однофайловый режим `-fsingle-file`/`-fno-single-file`:** обёртка `int main` (builder
  `buildEntryMainSource`) встраивается в тот же `.cppt` модуля; отдельный `_main.cppt` и `SRC_MAIN`
  в build.conf не создаются (writeBuildFiles пропускает entry при `opts.single_file`). По умолчанию
  включён при `--run` (резолв - в pipeline_parser.cpp); `-fno-single-file` возвращает многофайловый
  путь. «Модуль-скрипт» (top-level код без `__main__`) реализован python-подобно: top-level операторы
  (вкл. `x := ...`) DeclEmitter оборачивает в синтезируемую entry `<модуль>__main__` (переменные скрипта -
  локальные в main); функции/типы/области/`{% %}`-embed остаются на namespace. Entry может задаваться и
  C++-embed (`{% int <модуль>__main__() {} %}`), поэтому при `!sawEntry` наличие проверяется по тексту `.cppt`.
- **Скачиваемый архив песочницы (`emitBuildDirArchive`, trust-lsp `--emit-build-dir`) — однофайловый**
  (`opts.single_file = true`, тот же путь, что `--run`): «модуль-скрипт» без `__main__` при этом
  компилируется (top-level → синтезируемая entry в том же `.cppt`, `_main.cppt`/`SRC_MAIN` нет).
  Многофайловый путь (`-o` без `-fsingle-file`) скрипт без main НЕ поддерживает (некомпилируемый C++).

- **Единый CLI: arity-aware парсер (DriverOption).** Опции драйверов (trust/trust-lsp/trust-dap):
  `enum + vector<DriverOption> + switch`. `parseDriverArgs` (header-only, `cli.hpp`) ест ровно заявленную
  арность; повторяемые `-l/-L` не «доедают» входной файл. `-W` применяются позже (единая точка).
  Категории Diagnostics нет — двухсправочная модель.
- **Общие опции анализа** (`--solver-mode`, `--keywords`, `-fsolver-loop-unroll`) — ОДИН раз
  (`commonAnalysisOptions`); trust-lsp их не объявляет — analysis_passthrough отдаёт неизвестные
  `--name=value`/`-fname` в `applyAnalysisArgs`.
- **Макросы:** `trust/dsl.src` загружается один раз в `Context` (`setMacro`); каждый Parser наследует
  Macro через `Context&`. `--dsl <file>` заменяет, `--no-dsl` отключает.
- **LSP-режим:** `allow_semantic_on_errors` (LSP включает всегда) разрешает семантику на частичном AST;
  Transpile при ошибках не выполняется. `releaseTypes()` отдаёт владение TypeRegistry вызывающему (чтобы
  TypeId в SymbolIndex оставался валидным).
- **Декомпозиция:** `pipeline.cpp` разбит на модульные TU; инвариант — только перестановка кода,
  `kEmbeddedDslSrc` (#embed dsl.src) остаётся в `pipeline.cpp`. `-Wno-c23-extensions` нужен и pipeline.cpp,
  и source_map.cpp (оба содержат #embed).
