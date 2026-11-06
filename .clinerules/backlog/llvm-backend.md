# LLVM-бэкенд: подключаемость и proof-эмиссия

> status: backlog (0.9)
> area: pipeline/backend, pipeline/run, driver, target
> architecture: R4, R37, E20; §1 (LLVM — обязательная возможность), §6 (полная реализация — вне ядра)

## Цель

Проверить, что нативный (LLVM) бэкенд реализуем на существующей модели без переписывания языковой
модели (R37): стадия читает HIR, целевой контракт — через аксессор владельца, артефакт эмитируется
в память; неподдерживаемые конструкции дают Trust-диагностику, а не тихий пропуск (R9).

Полная реализация (кодоген, layout/ABI, исполняемая матрица) — **вне** этого этапа и вне ядра (§6).

## As-is эвиденс (проверено по коду)

- **Слота запроса/результата нет.** `BackendRequest` знает только `cpp`/`smt`
  (`include/pipeline/backend/backend.hpp:90-91`); `StageOutputs` — только `cpp`/`header` (`:110-120`);
  `RunRequest` — только `cpp`/`smt` (`include/pipeline/run/types.hpp:38-39`, комментарий о том, что
  «присутствие optional = запрос», — `include/driver/options.hpp:27-28`).
- **Гейт фазы не пропустит LLVM-only прогон:** `src/pipeline/run/phases.cpp:300` —
  `if (!run.req.cpp && !run.req.smt) return NoWork;`, инициализация запроса там же (`:305-313`).
- **Триггера CLI нет:** единственные бэкенд-триггеры — `--emit-cpp` → `Pipeline::emitCppToStdout`
  (`src/trust.cpp:338-341`) и `--noop-backend` (флаг внутри уже запрошенного cpp-прогона,
  `include/driver/options.hpp:108-111`).
- **Место реализации не определено и противоречит раскладке:** контракт стадии живёт в
  internal-подкаталоге `pipeline` (`include/pipeline/CMakeLists.txt:7` — `INTERNAL_DIRS backend`), а
  `internal_boundary_guard` запрещает другому компоненту включать internal-подкаталог
  (`cmake/check_internal_boundary.cmake:4-9`). Комментарий `backend.hpp:7-8` обещает «LLVM — новый
  компонент», что в текущей раскладке невозможно без выноса контракта в публичный компонент.
- **Проверок бэкенда в сборке нет:** маркерные гейты (`backend_purity_guard`, `emission_mapping_guard` и др.)
  удалены из системы сборки (AGENTS.md §16; `cmake/MEMORY.md`), поэтому DoD для LLVM-кода не может опираться
  на них — чистота бэкенда и полнота маппинга проверяются тестами (`test/`), а не гейтами сборки.
- **R40 не определён для LLVM-артефакта:** «артефакт без карты запрещён» (`ARCHITECTURE.md` R40);
  для текстового `.ll` карта возможна, для `.bc`/`.o` — нет. Следовательно proof обязан быть
  in-memory (не артефакт), иначе требуется маппинг.
- **Целевой контракт есть, значений triple/features нет:** владелец — `target::TargetContract`
  (`include/target/target.hpp:18-41`), аксессор `targetInfo()`; но `targetInfoFromOptions`
  (`include/pipeline/run/target_options.hpp:11-15`) заполняет только `provider` (= хост-компилятор C++),
  а `triple`/`features` остаются пустыми (закреплено тестом `test/unit/pipeline/target_info_test.cpp:16-20`).
  «Пусто — хост» (`include/target/target_info.hpp:19`) обязан разрешаться у владельца контракта, а не в
  стадии (R9: подстановка в потребителе — silent fallback).
- **LLVM уже обязателен и частично линкуется:** `find_package(LLVM REQUIRED CONFIG)`
  (`cmake/dependencies.cmake:48-50`), `LLVMSupport` линкуется в `utils`/`sourcemap`
  (`include/utils/CMakeLists.txt:16`); в системе есть `libLLVM-22.so` и `llvm/IR/IRBuilder.h`.
  Новое — линковка `LLVMCore` в `pipeline_lib` (влияние на `trust`, LSP, `unit_tests`).

## Скоуп

### 20a. Контракт + proof-эмиссия в память (объём этой задачи)

1. **Контракт (согласование API — главная цель):**
   `backend::LlvmTarget` (без пути — артефакт только в памяти), `backend::LlvmArtifact{std::string ir;}`,
   `BackendRequest::llvm` (optional = запрос), `StageOutputs::llvm`,
   строка `M(Llvm, "llvm", needsHir=true, needsMir=false, stopOnFailure=true, llvmStageRequested, runLlvmStage)`
   в `BACKEND_STAGE_LIST` (перед `Noop`), `RunRequest::llvm`.
2. **Триггер и use-case:** `EmitFlags::Llvm` + `--emit-llvm` → `Pipeline::emitLlvmToStdout(inputFile)`
   (по образцу `emitCppToStdout`, без записи файла); в `phases.cpp` перечисление
   `!cpp && !smt` заменить предикатом «запрошена хотя бы одна стадия» (данные запроса, не `if` на бэкенд).
3. **Разрешение дефолтов контракта — у владельца:** «пустой triple = хост» вычисляется в
   `targetInfoFromOptions`/конструкторе `TargetContract`, стадия только читает.
4. **Эмиттер:** partial-pass таблица по kind'ам с явным списком поддержки — функции с `Void`/`Int64` в
   сигнатуре и телом `return <int literal|param>`; всё остальное — Trust-диагностика с фиксированным
   текстом и `StageOutcome::Error`. Порядок функций — порядок HIR (R42). `llvm::verifyModule` внутри
   стадии (проверка себя, а не потребителя).
5. **Тесты:** unit — прогон `Pipeline::run` с `req.llvm` на `.src`-фикстуре даёт `result.llvm->ir` с
   `define`/именем/`ret`; негативный unit — конструкция вне списка поддержки даёт диагностику, `Error` и
   отсутствие артефакта; лит `test/lit/pipeline/e2e/backend/llvm_emit.src` с `--emit-llvm` + FileCheck;
   имя/порядок стадии в `backend_stage_test.cpp`; «тест удаления» для новых полей контракта.

**DoD 20a:** `make build` — 0 ошибок; `timeout 900 make test` — все зелёные; память зон
`pipeline/backend`, `pipeline/run`, `target`, `driver` синхронизирована; выбранный способ линковки
LLVM (`LLVM_LINK_LLVM_DYLIB` vs component libs) зафиксирован в MEMORY зоны.

**Не-цели 20a:** сохранение файла и R40-маппинг; вызовы/runtime/`RuntimeSymbol`; lowering типов кроме
`Int64`/`Void`; R47/R52; `toolchain` (E20); новый компонент (стадия живёт в `src/pipeline/backend/`).

### 20b. Артефакт `.ll` + маппинг (будущий этап, отдельная задача)

Сохранение текстового модуля как артефакта с картой исходник→LLVM IR (R40), `DataLayout`/triple из
контракта, опции `--target`/feature-set с реальным источником значений.

### 20c. Целевая выполнимость (будущий этап, отдельная задача)

Сборка/линковка/исполнение артефакта в целевой матрице (R47/R52). Сборка — `toolchain`/`builddriver`,
в конвейер не входит (E20).

## Риски

- Линковка `LLVMCore` в `pipeline_lib` утяжеляет `trust`, LSP и `unit_tests` (время линковки/размер) —
  решение о dylib/component libs принимается в 20a и фиксируется.
- `triple`/`features` без источника значений: proof использует только `cpp_standard`/`provider`;
  молчаливая подстановка хоста в стадии запрещена (R9) — дефолт у владельца контракта.
- Расширение `StageOutputs`/`BackendRequest` — правка контракта для всех стадий (агрегатная
  инициализация `BackendRequest` в `phases.cpp:305`): одна точка, но её нельзя пропустить.
- Искушение пролезть в type lowering/layout: proof обязан остаться на `Int64`/`Void`, иначе объём
  неограничен и R37/R4 начинают нарушаться незаметно.

## Отвергнутые варианты

- «Полная реализация LLVM сразу»: type lowering, layout/ABI/mangling, runtime-символы, матрица — объём
  неограничен; целевая модель фиксирует только подключаемость (§6).
- «Добавить маппинг C++-фактов для LLVM»: второй бэкенд станет заложником реестра `types` (R37).
- «Ограничиться существующей стадией `Noop`»: она доказывает только подключение строкой таблицы, но не
  достаточность HIR/контракта для реальной эмиссии — нового знания не даёт.
- «Встроить сборку LLVM-артефакта в конвейер»: сборка — слой `toolchain`/`builddriver` (E20).
- «Вынести контракт бэкенда в публичный компонент в этом же этапе»: смешивает две цели (API proof и
  раскладка/линковка); при необходимости — отдельная задача.

## Ссылки

`ARCHITECTURE.md` §1 (LLVM — обязательная возможность), §3.F (R37, R40, R47, R52), §3.I (E20), §4
(строка «Целевой контракт»), §6 (полная реализация LLVM — вне ядра); `include/pipeline/backend/MEMORY.md`;
`include/target/MEMORY.md`; `include/pipeline/run/MEMORY.md`.