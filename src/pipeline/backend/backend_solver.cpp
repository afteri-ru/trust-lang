#include "pipeline/backend/stages.hpp"

#include "pipeline/run/types.hpp" // PipelineResult: агрегат прогона с SMT-скриптом (владелец — оркестратор)
#include "session/run_services.hpp"
#include "solver/smt_printer.hpp"
#include "solver/trust_to_smt.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace trust {
namespace backend {

namespace {

bool saveSmtFile(const RunServices& services, const SmtTarget& target, const solver::SmtScript& script) {
    const std::string smt2 = solver::SmtPrinter::printScript(script);
    std::ofstream ofs(target.path);
    if (!ofs) {
        trust::errs() << "error: cannot write solver file: " << target.path << "\n";
        return false;
    }
    ofs << smt2;
    trust::errs() << "info: wrote SMT-LIB 2 file: " << target.path << "\n";
    std::filesystem::path mapPath = target.path;
    mapPath += ".map";
    std::ofstream map_ofs(mapPath);
    if (!map_ofs) {
        trust::errs() << "error: cannot write solver map file: " << mapPath << "\n";
        return false;
    }
    map_ofs << solver::SmtPrinter::buildSmt2Map(services, script, smt2);
    trust::errs() << "info: wrote SMT-LIB 2 map: " << mapPath << "\n";
    return true;
}

} // namespace

bool solverStageRequested(const BackendRequest& req) noexcept {
    return req.smt.has_value();
}

StageOutcome runSolverStage(BackendServices& services, const BackendRequest& req, StageOutputs& out) {
    // SMT-скрипт — не codegen-артефакт контракта: его читает и печатает/исполняет оркестратор,
    // поэтому стадия пишет в СВОЙ агрегат (StageOutputs полиморфен, тип владельца — PipelineResult).
    auto* result = dynamic_cast<PipelineResult*>(&out);
    EXPECT(result != nullptr && "solver stage requires the PipelineResult aggregate");
    EXPECT(req.hir != nullptr && "solver stage requires HIR");
    EXPECT(req.mir != nullptr && "solver stage requires MIR");
    EXPECT(req.smt.has_value() && "solver stage requested without SmtTarget");

    solver::TrustToSmt bridge(services.services, req.modes);
    auto script = bridge.generate(*req.hir, *req.mir);
    if (!script) {
        return StageOutcome::Error;
    }

    SmtArtifact artifact;
    artifact.script = std::move(*script);
    result->smt = std::move(artifact);

    if (result->smt->script.commands.empty()) {
        return StageOutcome::NoWork;
    }
    if (req.smt->path.empty()) {
        return StageOutcome::Done;
    }
    result->smt->saved = saveSmtFile(services.services, *req.smt, result->smt->script);
    return result->smt->saved ? StageOutcome::Done : StageOutcome::Error;
}

} // namespace backend
} // namespace trust
