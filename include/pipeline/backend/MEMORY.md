# MEMORY.md

> scope: include/pipeline/backend (+ src/pipeline/backend)
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Internal-подкаталог `pipeline`: контракт бэкенда и его реализации.
- `backend.hpp` — value-типы стадий: `CppTarget`/`SmtTarget`, `CppArtifact`/`HeaderArtifact`,
  `StageOutcome`, `BackendServices` (`RunServices`/`PipelineOpts`/`ModuleLoader`), `BackendRequest`
  (HIR/MIR/режимы/цели/указатель на целевой контракт), `StageOutputs`, `CodegenRequest`.
- `stages.hpp` — декларативная таблица стадий (`BackendKind`, `BACKEND_STAGE_LIST`, `BackendStage`,
  `backendStageName`, `runBackendStages`, `emitCppArtifact`).
- Реализации: оркестратор `runBackends` и стадии `Solver` (`backend_solver.cpp`), `Verify`
  (`backend_verify.cpp`), `Cpp` (`backend_cpp.cpp`), стаб `Noop` (`backend_noop.cpp`, R37 —
  подключается строкой таблицы, без `transpiler`/`stdlib`), emit/checks (`emit.cpp`, `checks.cpp`).
SMT-стадия опирается на `solver`, C++-стадия — на `transpiler`; контракт и реализации живут в одном
internal-подкаталоге, отдельного компонента контракта нет.

## Facts and invariants

- **⚠ Контракт не знает `PipelineResult`:** стадия получает `BackendRequest` и пишет в `StageOutputs`;
  оркестратор НАСЛЕДУЕТ `StageOutputs` и хранит свои артефакты (SMT-скрипт). Стадия, которой нужен
  собственный агрегат, приводит к типу владельца (`dynamic_cast` + `EXPECT` — тихой потери нет).
- **⚠ Порядок стадий = строки `BACKEND_STAGE_LIST`:** `NoWork` не останавливает конвейер, `Error` —
  только при `stopOnFailure`; имена и полнота `BackendKind` производятся из того же x-macro.
- **⚠ Предпосылки проверяет конвейер:** `needsHir`/`needsMir` при `nullptr` в запросе дают внутреннюю
  ошибку (ICE-политика), а не тихий пропуск стадии.
- `BackendRequest::hir/mir` — невладеющие указатели; `nullptr` = представление не построено
  (прогон без семантики/после ICE).
- **Артефакты пишут сами стадии,** вызванные `Pipeline`; `solver` от `pipeline` не зависит.
- **⚠ Проверка необработанных атрибутов** — `checks` по HIR до/после кодогена; финальный `-Wunhandled-attr`
  идёт по HIR.
- **⚠ Целевой контракт стадии — `BackendRequest::target` (невладеющая ссылка на `TargetContract`), а не
  `PipelineOpts`:** значения целевой платформы считаются ОДИН раз при создании контракта
  (`pipeline/run/target_options.hpp` → конструктор `TargetContract`) и хранятся у владельца; стадия читает
  `target->targetInfo()` и ничего не пересчитывает. Стандарт C++ для `build.conf` (`-std=`) берётся сборкой
  из того же контракта (`Pipeline::targetContract()`), в артефакте копии нет (R37/E20, единый источник).

## Decisions

- Бэкенд подключается ДАННЫМИ (таблица стадий + value-типы), а не правкой оркестратора: новый бэкенд
  (напр. LLVM) добавляется строкой `BACKEND_STAGE_LIST`, а не `if`-веткой в `Pipeline`.

## Relations

- `transpiler` (`Cpp`), `solver` (`Solver`), `ir` (HIR/MIR), `pipeline/run` (запрос/результат),
  `analysis` (режимы), `driver` (`PipelineOpts`).
