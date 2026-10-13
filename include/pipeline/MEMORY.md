# MEMORY.md

> scope: include/pipeline (public API)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 7000

## Architecture

Оркестратор компиляции: Parser (Flex/Bison; DSL через `ParseText`) → `TermToAstConverter` (loader-free) →
semantic (ядро → `materializeArgCasts` → `verifyTypedAst`) → `buildHir` → `verifyHir` → `buildMir` →
DA/borrow по MIR → transpile → сборка C++ через `toolchain` (Makefile/`build.conf`). Прогон описывается `RunRequest`
и возвращает `PipelineResult`; файлы артефактов пишет только сам `Pipeline` и только по непустому
target-пути (пустой = артефакт остаётся в памяти, LSP/playground). Параметры кодогенерации берутся из
`PipelineOpts`, а не из target. Публичный API — `pipeline.hpp` (umbrella: value-типы, forward declarations
и реэкспорт archive/dsl/paths/sourcemap/symbols), `cli.hpp`, `commands.hpp`.

Детали реализации — в internal-подкаталогах:

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `pipeline/archive/` | — (инварианты в коде) | tar.gz-архив build-каталога (`--emit-build-dir`) |
| `pipeline/backend/` | `backend/MEMORY.md` | контракт стадий + реализации (C++/SMT/Verify), emit/checks, оркестрация |
| `pipeline/dsl/` | — (инварианты в коде) | DSL-макросы, stdlib prelude, эффективные keywords |
| `pipeline/modules/` | `modules/MEMORY.md` | модульные юниты, заголовки `.hpp`/`.cppt` |
| `pipeline/paths/` | — (инварианты в коде) | расчёт путей build-каталога и `.cppt`/`.hpp` |
| `pipeline/run/` | `run/MEMORY.md` | `RunRequest`/`PipelineResult`, single-file, LSP-режим, DSL-bootstrap |
| `pipeline/sourcemap/` | — (инварианты в коде) | сохранение `.cppt` + `#embed` source-map, ELF-секции |

## Facts and invariants

- **При errorCount>0 после ParseAST semantic/transpile НЕ запускаются** (AST — восстановленный префикс
  после parser error recovery, а не только обрезанный). Фиктивные in-memory источники помечаются в
  source map префиксом `@`.
- **⚠ Резолв путей и групповой импорт:** тип первого компонента (`\` файл, `\\` модуль, `\\\` пакет),
  scope root (project/package), `-I`/`--package-path`; build-каталог — не корень поиска. Групповой
  импорт нерекурсивный и разворачивается в файловые импорты; `dir.src` и `dir/` одновременно — ошибка.
- **⚠ `checkExportedGenericDeclarations` — пост-проход Pipeline:** exported generic без тела и без
  определения в ТОМ ЖЕ модуле — ошибка Trust, а не отложенная ошибка C++.
- **⚠ `PipelineResult::strings` — владение текстами результата:** `shared_ptr<StringInterner>` (пул
  `Context::strings()`), разделяется с AST/HIR/`SemanticModel` и держит тексты валидными после
  разрушения `Context`. `PipelineResult::semantics` — модель семантики (persistent-таблица +
  проекции), строится при шаге `Semantic`.
- **⚠ Декомпозиция:** `pipeline.hpp` — только value-типы + forward declarations; модульные заголовки
  самодостаточны (без «зонтика»). Prelude и пакетные заголовки — из `share/trust/stdlib.trust` в
  `<build>/stdlib`; `-Wno-c23-extensions` нужен файлам с `#embed`.

## Relations

- `semantic`, `transpiler`, `ir`, `solver`, `toolchain`, `module_loader`, `symbols`, `driver`, `package`, `formatter`;
  диспетчеризация команд — `trust.cpp` (composition root).
