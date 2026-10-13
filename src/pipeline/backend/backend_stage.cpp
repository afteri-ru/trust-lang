#include "pipeline/backend/stages.hpp"

#include "semantic/analysis_api.hpp"

#include <format>
#include <span>
#include <string_view>

namespace trust {
namespace backend {

bool runBackendStages(std::span<const BackendStage> stages, BackendServices& services, const BackendRequest& req, StageOutputs& out) {
    for (std::size_t index = 0; index < stages.size(); ++index) {
        const BackendStage& stage = stages[index];
        if (!stage.requested(req)) {
            continue;
        }
        const std::string_view stage_name =
            index < static_cast<std::size_t>(BackendKind::kCount) ? backendStageName(static_cast<BackendKind>(index)) : std::string_view{"backend"};
        if (stage.needsHir && req.hir == nullptr) {
            reportInternalError(services.ctx, services.opts.allow_semantic_on_errors, std::format("backend stage '{}' requires HIR", stage_name));
            return false;
        }
        if (stage.needsMir && req.mir == nullptr) {
            reportInternalError(services.ctx, services.opts.allow_semantic_on_errors, std::format("backend stage '{}' requires MIR", stage_name));
            return false;
        }
        const StageOutcome outcome = stage.run(services, req, out);
        if (outcome == StageOutcome::Error && stage.stopOnFailure) {
            return false;
        }
    }
    return true;
}

} // namespace backend
} // namespace trust
