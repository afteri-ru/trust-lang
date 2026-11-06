#pragma once

// Backend-конвейер: упорядоченная таблица стадий, которые читают HIR/MIR после семантики.
// Инварианты:
//   * порядок стадий = порядок в BACKEND_STAGE_LIST (Solver → Cpp);
//   * имена стадий производятся из того же списка (не хранятся в таблице);
//   * StageOutcome::NoWork («нечего делать») НЕ останавливает конвейер;
//   * StageOutcome::Error останавливает конвейер, если у стадии stopOnFailure == true.
// Реализации стадий и `runBackendStages` живут у владельцев (pipeline); компонент `backend`
// — только контракт (INTERFACE-библиотека), поэтому новых link-рёбер не появляется.

#include "pipeline/backend/backend.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace trust::backend {

/// Идентичность стадии. Порядок значений совпадает с порядком строк BACKEND_STAGE_LIST.
enum class BackendKind { Solver, Cpp, Verify, Noop, kCount };

struct BackendStage {
    bool needsHir = true;
    bool needsMir = false;
    bool stopOnFailure = true;
    bool (*requested)(const BackendRequest&) noexcept;
    StageOutcome (*run)(BackendServices&, const BackendRequest&, StageOutputs&);
};

/// Список стадий: ЕДИНСТВЕННЫЙ источник BackendKind, имён и таблицы исполнения.
#define BACKEND_STAGE_LIST(M)                                                                                               \
    M(Solver, "solver", /*needsHir=*/true, /*needsMir=*/true, /*stopOnFailure=*/true, solverStageRequested, runSolverStage) \
    M(Cpp, "cpp", /*needsHir=*/true, /*needsMir=*/false, /*stopOnFailure=*/true, cppStageRequested, runCppStage)            \
    M(Verify, "verify", /*needsHir=*/true, /*needsMir=*/true, /*stopOnFailure=*/true, verifyStageRequested, runVerifyStage) \
    M(Noop, "noop", /*needsHir=*/true, /*needsMir=*/false, /*stopOnFailure=*/true, noopStageRequested, runNoopStage)

bool solverStageRequested(const BackendRequest& req) noexcept;
bool cppStageRequested(const BackendRequest& req) noexcept;
StageOutcome runSolverStage(BackendServices& services, const BackendRequest& req, StageOutputs& out);
StageOutcome runCppStage(BackendServices& services, const BackendRequest& req, StageOutputs& out);
bool verifyStageRequested(const BackendRequest& req) noexcept;
StageOutcome runVerifyStage(BackendServices& services, const BackendRequest& req, StageOutputs& out);
/// Стаб-стадия бэкенда (R37): подключается строкой таблицы, потребляет `BackendRequest` данными.
bool noopStageRequested(const BackendRequest& req) noexcept;
StageOutcome runNoopStage(BackendServices& services, const BackendRequest& req, StageOutputs& out);

inline constexpr std::array<BackendStage, static_cast<std::size_t>(BackendKind::kCount)> kBackendStages = {{
#define BACKEND_STAGE_ROW(Name, label, needsHir, needsMir, stopOnFailure, requestedFn, runFn) {needsHir, needsMir, stopOnFailure, requestedFn, runFn},
    BACKEND_STAGE_LIST(BACKEND_STAGE_ROW)
#undef BACKEND_STAGE_ROW
}};

inline constexpr std::string_view kBackendStageNames[] = {
#define BACKEND_STAGE_NAME(Name, label, ...) label,
    BACKEND_STAGE_LIST(BACKEND_STAGE_NAME)
#undef BACKEND_STAGE_NAME
};

#undef BACKEND_STAGE_LIST

static_assert(kBackendStages.size() == static_cast<std::size_t>(BackendKind::kCount), "BACKEND_STAGE_LIST must cover every BackendKind");
static_assert(std::size(kBackendStageNames) == static_cast<std::size_t>(BackendKind::kCount), "BACKEND_STAGE_LIST must provide a name for every BackendKind");

[[nodiscard]] constexpr std::string_view backendStageName(BackendKind kind) noexcept {
    return kBackendStageNames[static_cast<std::size_t>(kind)];
}

/// Исполняет запрошенные стадии в порядке таблицы. false — конвейер остановлен
/// (ошибка стадии с stopOnFailure или нарушение предпосылки).
bool runBackendStages(std::span<const BackendStage> stages, BackendServices& services, const BackendRequest& req, StageOutputs& out);

/// C++-кодогенерация одной единицы трансляции (реализация — C++-бэкенд в `pipeline`).
CppArtifact emitCppArtifact(BackendServices& services, const CodegenRequest& request);

} // namespace trust::backend
