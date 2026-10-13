# MEMORY.md

> scope: include/pipeline/backend (+ src/pipeline/backend)
> role: persistent-memory
> last_reviewed: 2026-11-05
> review_period: 30
> max_size: 5000

## Architecture

Internal-подкаталог `pipeline`: контракт бэкенда и его реализации.
- `backend.hpp` — value-типы стадий: `CppTarget`/`SmtTarget`, `CppArtifact`/`HeaderArtifact`,
  `StageOutcome`, `BackendServices` (`Context`/`PipelineOpts`/`ModuleLoader`), `BackendRequest`
  (HIR/MIR/режимы/цели), `StageOutputs`, `CodegenRequest`, `TargetInfo`.
- `stages.hpp` — декларативная таблица стадий (`BackendKind`, `BACKEND_STAGE_LIST`, `BackendStage`,
  `backendStageName`, `runBackendStages`, `emitCppArtifact`).
- Реализации: оркестратор `runBackends` и стадии `Solver` (`backend_solver.cpp`), `Verify`
  (`backend_verify.cpp`), `Cpp` (`backend_cpp.cpp`), emit/checks (`emit.cpp`, `checks.cpp`).
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
- **Проверка необработанных атрибутов** — `checks` по HIR до/после кодогена; финальный `-Wunhandled-attr`
  идёт по HIR.

## Decisions

- Бэкенд подключается ДАННЫМИ (таблица стадий + value-типы), а не правкой оркестратора: новый бэкенд
  (напр. LLVM) добавляется строкой `BACKEND_STAGE_LIST`, а не `if`-веткой в `Pipeline`.

## Relations

- `transpiler` (`Cpp`), `solver` (`Solver`), `ir` (HIR/MIR), `pipeline/run` (запрос/результат),
  `analysis` (режимы), `driver` (`PipelineOpts`).
