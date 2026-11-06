# Pipeline - Transpiler Pipeline

## Назначение

Драйвер полного конвейера компиляции: чтение исходного кода → лексический анализ → макропроцессинг → синтаксический анализ → семантический анализ → транспиляция в C++. Сборку/линковку C++ выполняет нижестоящий драйвер `builddriver` (слой `toolchain`).

## Особенности реализации

- **Полный конвейер** — единая декларативная таблица фаз `PIPELINE_PHASE_LIST` (`pipeline/run/phases.hpp`):
  ReadParse → Semantic → LowerHir → BuildMir → FlowAnalyses → Finalize → Backends. Оркестратор
  `Pipeline::runPhases` условий реализации не содержит (порядок = порядок строк x-macro, NoWork/остановку
  решают сами фазы; гейты остановки — `PhaseRun::stopped`).
- **FrontendSteps** — публичный ЗАПРОС шагов (`ParseAST`, `Semantic`), а не описание порядка фаз.
  Бэкенды (`Cpp`, `Smt`) шагами не являются — запрашиваются через `RunRequest`.
- **Backend-конвейер** — контракт `backend/stages.hpp` (`include/backend/`) + `pipeline/backend/backend_stage.cpp`: упорядоченная таблица стадий `Solver` (side output, `needsHir`+`needsMir`) → `Cpp` (primary codegen, `needsHir`). Список — x-macro `BACKEND_STAGE_LIST` (единственный источник `BackendKind`, имён и таблицы). `StageOutcome::NoWork` не останавливает конвейер, `Error` останавливает при `stopOnFailure`. Оркестратор (`runBackendStages`) не содержит условий реализации.
- **RunRequest / PipelineResult** — value-типы в `pipeline/run/types.hpp`: `steps` × `mode` × `optional<CppTarget>`/`optional<SmtTarget>`; результат — AST/символы/артефакты. `astNodes` — неизменённая AST (pre-lowering, её читает solver), `hirNodes` — HIR (`HirModule`: клон AST + lowering, вход C++-бэкенда); HIR строит LowerHir-фаза (`buildLoweredHir`: buildHir → `verifyHir`). Заголовок `pipeline.hpp` не включает модульные заголовки (нет «зонтика»).
- **`Pipeline::run`** — единственная публичная точка прогона фронтенда/бэкендов; `runCppToFile`/`emitCppToStdout` — use-cases emit (артефакты). Сборка — в `builddriver`. **Stateless** — AST/артефакты не кешируются в классе.
- **Команды CLI** — `pipeline/commands.hpp` + `commands/commands.cpp` (лексемы, solver, emit, компиляция); диспетчеризация — в `trust.cpp` (composition root). Для `--format`/`--package`/`--extract`/completion `Pipeline` не создаётся.
- **Разбор argv** — `pipeline/cli.hpp` + `cli/cli.cpp` (`parseCommandLine`/`isSpecialExit`), arity-aware таблицы — `driver/cli.hpp`.
- **Bootstrap фронтенда** — `pipeline/dsl/dsl.hpp` + `dsl/dsl.cpp` (DSL-макросы, эффективные keywords).
- **Модульные единицы** — `pipeline/modules/modules.cpp` (`buildModuleRoot`, `runModuleUnit`, `generateModuleOutputs`, `runCppToFile`); `import_exports` — в `module_loader`.
- **Резолв модулей/пакетов** — `module_loader`: prefix-type пути (`\`/`\\`/`\\\`), scope root, `-I`/`--package-path`; групповой импорт разворачивается в файловые импорты; build-каталог не корень поиска.
- **Кодогенерация/emit** — C++-артефакт `backend/backend_cpp.cpp` (`emitCppArtifact`), SMT — `backend/backend_solver.cpp`; `backend/emit.{hpp,cpp}` — emit-вывод и заголовки модулей; проверка необработанных атрибутов — `backend/checks.{hpp,cpp}`.
- **Сборка** — НЕ в `pipeline`: оркестрация сборки/запуска (слой `toolchain`, Makefile/build.conf/entry) — в нижестоящем `builddriver`; `pipeline` производит артефакты (`.cppt`/`.hpp`).
- **Пути** — `pipeline/paths.{hpp,cpp}` (`computeBuildDir`, `computeCpptPath`).
- **Обработка ошибок** — все ошибки проходят через `diag`, с немедленным прерыванием при фатальных.
