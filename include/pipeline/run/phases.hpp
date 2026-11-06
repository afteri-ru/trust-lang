// Декларативный конвейер фаз: единая таблица фаз фронтенда и бэкендов прогона.
// Контракт (по образцу `pipeline/backend/stages.hpp`):
//   * порядок фаз = порядок строк PIPELINE_PHASE_LIST (x-macro — единственный источник
//     PhaseKind, имён и таблицы исполнения);
//   * фаза получает состояние прогона `PhaseRun` и возвращает `StageOutcome`:
//       NoWork — фаза не должна выполняться в этом прогоне (конвейер продолжается);
//       Done   — работа выполнена;
//       Error  — ошибка уже диагностирована; конвейер останавливается;
//   * динамические условия (crashed/errorCount/`--verify`) — ВНУТРИ фазы, а не в
//     оркестраторе: `Pipeline::runPhases` не содержит условий реализации;
//   * гейты остановки фронтенда (`PhaseRun::stopped`) владеют фазы: бэкенды не запускаются
//     на недостоверном представлении.
// Макрос PIPELINE_PHASE_LIST оставлен определённым: таблицу исполнения (`&Pipeline::fn`)
// разворачивает `src/pipeline/run/phases.cpp` (как `TRUST_OBLIGATION_KIND_IDS` в verify).
#pragma once

#include "pipeline/run/types.hpp"

#include "ast/ast_nodes.hpp"
#include "driver/options.hpp"
#include "ir/cfg.hpp"
#include "ir/hir_module.hpp"
#include "location/module_index.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace trust {

class Pipeline;
class SemanticPassRunner;

/// Единственный источник PhaseKind, имён и таблицы исполнения фаз.
#define PIPELINE_PHASE_LIST(M)                             \
    M(ReadParse, "read-parse", runReadParsePhase)          \
    M(Semantic, "semantic", runSemanticPhase)              \
    M(LowerHir, "lower-hir", runLowerHirPhase)             \
    M(BuildMir, "build-mir", runBuildMirPhase)             \
    M(FlowAnalyses, "flow-analyses", runFlowAnalysesPhase) \
    M(Finalize, "finalize", runFinalizePhase)              \
    M(Backends, "backends", runBackendsPhase)

/// Идентичность фазы. Порядок значений совпадает с порядком строк PIPELINE_PHASE_LIST.
enum class PhaseKind {
#define PIPELINE_PHASE_ENUM(Name, label, fn) Name,
    PIPELINE_PHASE_LIST(PIPELINE_PHASE_ENUM)
#undef PIPELINE_PHASE_ENUM
        kCount
};

inline constexpr std::string_view kPhaseNames[] = {
#define PIPELINE_PHASE_NAME(Name, label, fn) label,
    PIPELINE_PHASE_LIST(PIPELINE_PHASE_NAME)
#undef PIPELINE_PHASE_NAME
};

static_assert(std::size(kPhaseNames) == static_cast<std::size_t>(PhaseKind::kCount), "PIPELINE_PHASE_LIST must provide a name for every PhaseKind");

[[nodiscard]] constexpr std::string_view phaseName(PhaseKind kind) noexcept {
    return kPhaseNames[static_cast<std::size_t>(kind)];
}

/// Состояние прогона фаз: запрос + продукты фаз + гейты. Владение — оркестратор `Pipeline`.
struct PhaseRun {
    RunRequest req;                        ///< публичный запрос прогона (steps × mode × бэкенды)
    PipelineResult result;                 ///< astNodes/semantics; hirNodes/mir заполняет Finalize
    std::optional<HirModule> hir;          ///< продукт LowerHir (единственный держатель до публикации Finalize)
    std::optional<flow::MirModule> mir;    ///< продукт BuildMir (потребитель — FlowAnalyses; публикует Finalize)
    ModuleIndex mainModuleIndex{};         ///< индекс главного модуля (модуль входного файла)
    bool semanticOk = false;               ///< runner.run(): false — блокирующие ошибки ядра
    bool stopped = false;                  ///< конвейер остановлен (бэкенды не запускаются)
};

} // namespace trust
