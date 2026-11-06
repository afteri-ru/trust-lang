# MEMORY.md

> scope: include/pipeline (public API)
> role: persistent-memory
> last_reviewed: 2026-11-13
> review_period: 30

## Architecture

Оркестратор компиляции — ЕДИНАЯ декларативная таблица фаз `PIPELINE_PHASE_LIST` (`pipeline/run/phases.hpp`):
ReadParse → Semantic (ядро → `materializeArgCasts` → **provider-gate** → `verifyTypedAst`) → LowerHir (`buildHir`→`verifyHir`) →
BuildMir (`buildMir`→`verifyFlowModule`) → FlowAnalyses (DA → borrow по MIR) → Finalize (гейты фронтенда) → Backends (`BACKEND_STAGE_LIST`: Solver/Verify/Cpp).
Сборку/линковку C++ `pipeline` НЕ выполняет: он производит артефакты (`.cppt`/`.hpp`), а `toolchain` вызывает
нижестоящий драйвер `builddriver` (E20/R34). Оркестратор `runPhases` условий реализации НЕ содержит (порядок = порядок строк x-macro).
Прогон — `RunRequest`
→ `PipelineResult`; файлы артефактов пишет только `Pipeline` и только по непустому target-пути (пустой —
артефакт в памяти, LSP/playground). Параметры кодогенерации — из `PipelineOpts`. Публичный API — `pipeline.hpp`
(umbrella), `cli.hpp`, `commands.hpp`.

| internal-подкаталог | MEMORY | ответственность |
|---|---|---|
| `pipeline/backend/` | `backend/MEMORY.md` | контракт стадий + реализации (C++/SMT/Verify), emit/checks, оркестрация |
| `pipeline/cli/` | — (в коде) | разбор argv (`parseCommandLine`/`isSpecialExit`) |
| `pipeline/commands/` | — (в коде) | use-case команды поверх `Pipeline` (лексемы, solver, emit, компиляция) |
| `pipeline/dsl/` | — (в коде) | DSL-макросы, эффективные keywords |
| `pipeline/modules/` | `modules/MEMORY.md` | модульные юниты, заголовки `.hppt`/`.cppt` |
| `pipeline/paths/` | — (в коде) | расчёт путей build-каталога и `.cppt`/`.hppt` |
| `pipeline/run/` | `run/MEMORY.md` | `RunRequest`/`PipelineResult`, фазы (`phases.hpp`), оркестратор `Pipeline`, single-file, LSP-режим |
| `pipeline/sourcemap/` | — (в коде) | сохранение `.cppt` + встраивание source-map байтовым массивом, ELF-секции |

## Facts and invariants

- **⚠ Активный пул строк прогона — на всё время жизни `Pipeline`** (`m_stringsScope`, включая ctor и
  `loadDslMacros` вне `run`): тексты/доки прогона принадлежат `Context::strings()`, процессный пул не
  задействован (E9). `Parser` ставит тот же пул из `ParserServices::strings()` для любого разбора.
- **⚠ Единый источник порядка фаз — `PIPELINE_PHASE_LIST`** (единственный источник `PhaseKind`, имён, таблицы
  исполнения; как `BACKEND_STAGE_LIST` для бэкендов). `FrontendSteps` (driver) — публичный ЗАПРОС (биты), не
  описание порядка. Динамические условия (crashed/errorCount/`--verify`) — внутри фаз (NoWork/остановка).
  Гейты остановки фронтенда (`PhaseRun::stopped`) владеют фазы: бэкенды не запускаются на недостоверном
  представлении.
- **При errorCount>0 semantic/transpile после ParseAST НЕ запускаются** (AST — восстановленный префикс после
  parser error recovery). Фиктивные in-memory источники помечаются префиксом `@`.
- **⚠ Резолв путей и групповой импорт:** тип первого компонента (`\` файл, `\\` модуль, `\\\` пакет), scope
  root (project/package), `-I`/`--package-path`; build-каталог — не корень поиска. Групповой импорт
  нерекурсивный, разворачивается в файловые; `dir.src` и `dir/` вместе — ошибка.
- **⚠ `checkExportedGenericDeclarations` — пост-проход Pipeline:** exported generic без тела и без определения
  в ТОМ ЖЕ модуле — ошибка Trust, а не отложенная ошибка C++.
- **⚠ `PipelineResult::strings` — владение текстами:** `shared_ptr<StringInterner>` (пул `Context::strings()`),
  разделяется с AST/HIR/`SemanticModel`, держит тексты валидными после разрушения `Context`.
- **⚠ Декомпозиция:** `pipeline.hpp` — только value-типы + forward declarations; модульные заголовки
  самодостаточны. Prelude/пакетные заголовки — из `share/trust/stdlib.trust` в `<build>/stdlib`.
  Встраиваемые ассеты (LICENSE, HTML/JS/CSS, карта исходников в `.cppt`) — байтовые массивы:
  `.inc` из `cmake/embed_assets.cmake` для своих TU и C++-инициализатор для эмитируемой карты
  (стандартный C++ — целевая матрица включает gcc, R47).
- **⚠ Один `Pipeline` — одна компиляция:** `run()` запрещён повторно (`m_hasRun` + `EXPECT`). Per-run
  владельцы (`TargetContract`, `SourceMapWriter`, `ModuleLoader`) создаются конструктором и не сбрасываются
  вместе с `TypeSystem`; новый прогон — новый `Pipeline`/`Context`.
- **⚠ Фронтенд цельно-программный, кеша разбора/семантики нет:** повторный `--run` коротко замыкается
  `tryRunCached` (ELF-секция `.debug_trust_hash`) — драйвер `builddriver`.
  Кеш только на финальной C++-сборке. Персистентная
  сериализация интерфейсов (P3) не реализуется.

## Relations

- `semantic`, `transpiler`, `ir`, `solver`, `module_loader`, `symbols`, `driver`, `package`,
  `formatter`; диспетчеризация команд — `trust.cpp`. НЕ зависит от `toolchain`: сборка/линковка — в
  драйвере `builddriver` (E20/R34).
