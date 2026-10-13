# Pipeline - Transpiler Pipeline

## Назначение

Драйвер полного конвейера компиляции: чтение исходного кода → лексический анализ → макропроцессинг → синтаксический анализ → семантический анализ → транспиляция в C++ → сборка через Makefile.

## Особенности реализации

- **Полный конвейер** — оркестрация этапов: read, lex, mmproc, parse, semantic, transpile, compile & link.
- **FrontendSteps** — битмаска шагов фронтенда (`ParseAST`, `Semantic`). Бэкенды (`Cpp`, `Smt`) шагами не являются — запрашиваются через `RunRequest`.
- **Backend-конвейер** — контракт `backend/stages.hpp` (`include/backend/`) + `pipeline/backend/backend_stage.cpp`: упорядоченная таблица стадий `Solver` (side output, `needsHir`+`needsMir`) → `Cpp` (primary codegen, `needsHir`). Список — x-macro `BACKEND_STAGE_LIST` (единственный источник `BackendKind`, имён и таблицы). `StageOutcome::NoWork` не останавливает конвейер, `Error` останавливает при `stopOnFailure`. Оркестратор (`runBackends`) не содержит условий реализации.
- **RunRequest / PipelineResult** — value-типы в `pipeline/run/types.hpp`: `steps` × `mode` × `optional<CppTarget>`/`optional<SmtTarget>`; результат — AST/символы/артефакты. `astNodes` — неизменённая AST (pre-lowering, её читает solver), `hirNodes` — HIR (`HirModule`: клон AST + lowering, вход C++-бэкенда); HIR строит `Pipeline::lowerAndAnalyze` после семантики (buildHir → `verifyHir` → DA/borrow), `Pipeline` отсекает prelude (`tailFrom`). Заголовок `pipeline.hpp` не включает модульные заголовки (нет «зонтика»).
- **`Pipeline::run`** — единственная публичная точка прогона фронтенда/бэкендов; `compileProgram` и `emitCppToStdout` — use-cases сборки и emit. **Stateless** — AST/артефакты не кешируются в классе.
- **Команды CLI** — `pipeline/commands.{hpp,cpp}` (лексемы, solver, emit, компиляция); диспетчеризация — в `trust.cpp` (composition root). Для `--format`/`--package`/`--extract`/completion `Pipeline` не создаётся.
- **Разбор argv** — `pipeline/cli.{hpp,cpp}` (`parseCommandLine`/`isSpecialExit`), arity-aware таблицы — `driver/cli.hpp`.
- **Bootstrap фронтенда** — `pipeline/dsl.{hpp,cpp}` (DSL-макросы, stdlib prelude, эффективные keywords).
- **Модульные единицы** — `pipeline/modules/modules.cpp` (`buildModuleRoot`, `runModuleUnit`, `generateModuleOutputs`, `runCppToFile`); `import_exports` — в `module_loader`.
- **Резолв модулей/пакетов** — `module_loader`: prefix-type пути (`\`/`\\`/`\\\`), scope root, `-I`/`--package-path`; групповой импорт разворачивается в файловые импорты; build-каталог не корень поиска.
- **Кодогенерация/emit** — C++-артефакт `backend_cpp.cpp` (`emitCppArtifact`), SMT — `backend_solver.cpp`; `pipeline/emit.{hpp,cpp}` — emit-вывод и заголовки модулей; проверка необработанных атрибутов — `pipeline/checks.{hpp,cpp}`.
- **Сборка** — слой `toolchain` (`toolchain/build.hpp`: `BuildRequest`/`BuildInputs`, Makefile/build.conf/entry; `compileAndLink`, runtime-окружение, установка артефакта). Входы сборки вычисляются однократно.
- **Пути** — `pipeline/paths.{hpp,cpp}` (`computeBuildDir`, `computeCpptPath`).
- **Обработка ошибок** — все ошибки проходят через `diag`, с немедленным прерыванием при фатальных.
