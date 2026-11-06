// Стадия `Verify`: собирает обязательства из плана SMT-бэкенда, исполняет решатель и пишет
// сертификат `*.trustcert`. Гейт `--verify=all`: непроверенное обязательство — ошибка.
#include "pipeline/backend/stages.hpp"

#include "session/run_services.hpp"
#include "solver/solver.hpp"
#include "solver/trust_to_smt.hpp"
#include "utils/error.hpp"
#include "utils/utils.hpp"
#include "verify/certificate.hpp"
#include "verify/proof_backend.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace trust {
namespace backend {

namespace {

/// Вид обязательства слоя доказательств по виду цели SMT-плана.
verify::ObligationKind mapObligationKind(solver::VcObligationKind kind) noexcept {
    switch (kind) {
    case solver::VcObligationKind::kInvariantEstablishment:
    case solver::VcObligationKind::kInvariantPreservation:
        return verify::ObligationKind::Invariant;
    case solver::VcObligationKind::kPost:
    case solver::VcObligationKind::kAssert:
    case solver::VcObligationKind::kCallPre:
    case solver::VcObligationKind::kTypeAssert:
        return verify::ObligationKind::Contract;
    }
    return verify::ObligationKind::Contract;
}

/// Статус обязательства по вердикту решателя для цели.
verify::ProofStatus mapProofStatus(solver::SolverResult result) noexcept {
    switch (result) {
    case solver::SolverResult::kUnsat:
        return verify::ProofStatus::Proved;
    case solver::SolverResult::kSat:
        return verify::ProofStatus::Failed;
    case solver::SolverResult::kUnknown:
    case solver::SolverResult::kUnsupported:
    case solver::SolverResult::kError:
        return verify::ProofStatus::Unknown;
    }
    return verify::ProofStatus::Unknown;
}

std::string_view solverVersion() noexcept {
#ifdef TRUST_HAS_Z3
    return "z3";
#else
    return "none";
#endif
}

bool writeCertificate(const std::filesystem::path& path, std::string_view text) {
    std::ofstream ofs(path);
    if (!ofs) {
        trust::errs() << "error: cannot write proof certificate: " << path << "\n";
        return false;
    }
    ofs << text;
    return true;
}

} // namespace

bool verifyStageRequested(const BackendRequest& req) noexcept {
    return req.verifyAll;
}

StageOutcome runVerifyStage(BackendServices& services, const BackendRequest& req, StageOutputs& out) {
    EXPECT(req.hir != nullptr && "verify stage requires HIR");
    EXPECT(req.mir != nullptr && "verify stage requires MIR");
    EXPECT(req.verifyAll && "verify stage requested without --verify");
    if (req.hir == nullptr || req.mir == nullptr) {
        return StageOutcome::Error;
    }

    // План целей строит SMT-бэкенд; модель обязательств и отчёт — `verify`.
    solver::TrustToSmt bridge(services.services, req.modes);
    auto script = bridge.generate(*req.hir, *req.mir);
    if (!script) {
        return StageOutcome::Error;
    }

    std::vector<verify::Obligation> obligations;
    std::vector<verify::ProofResult> results;
    std::string solver_result_note;

    if (!script->commands.empty()) {
        auto solver_ptr = solver::createSolver();
        solver::VerificationReport vc_report;
        const solver::SolverResult overall = solver::runScript(*solver_ptr, *script, /*out_model=*/nullptr, &vc_report);
        if (overall == solver::SolverResult::kError) {
            // Внутренняя ошибка кодирования/исполнения — не «непроверено», а отказ стадии.
            trust::errs() << "error: proof backend failed to execute the verification plan\n";
            return StageOutcome::Error;
        }
        solver_result_note = std::string(solver::SolverResult::kUnknown == overall ? "unknown" : "executed");
        for (const solver::VcCheckResult& check : vc_report.checks) {
            verify::Obligation obligation;
            obligation.kind = check.kind.has_value() ? mapObligationKind(*check.kind) : verify::ObligationKind::Contract;
            obligation.site = check.srcRange;
            obligation.goal = "verification condition";
            obligation.assumptions.emplace_back("contract axioms of other functions");
            obligations.push_back(std::move(obligation));

            verify::ProofResult result;
            result.status = mapProofStatus(check.result);
            result.model = check.model;
            results.push_back(std::move(result));
        }
    }

    // Цели вне области проверки: VC пропускается, но обязательство ОСТАЁТСЯ в отчёте.
    for (std::size_t i = 0; i < script->outsideEnvelopeCount; ++i) {
        verify::Obligation obligation;
        obligation.kind = verify::ObligationKind::Contract;
        obligation.goal = "verification condition (outside envelope)";
        obligation.envelope = "outside verification envelope (loop without invariant/unroll)";
        obligations.push_back(std::move(obligation));

        verify::ProofResult result;
        result.status = verify::ProofStatus::Envelope;
        results.push_back(std::move(result));
    }

    const verify::ProofReport report = verify::makeProofReport("smt", solverVersion(), obligations, std::move(results));

    if (req.certificatePath) {
        std::vector<verify::CertificateEntry> entries;
        entries.reserve(obligations.size());
        for (std::size_t i = 0; i < obligations.size(); ++i) {
            entries.push_back(verify::CertificateEntry{obligations[i], report.results[i].status});
        }
        const std::string certificate = verify::composeCertificate(req.inputHash, report.tool, report.version, entries);
        if (!writeCertificate(*req.certificatePath, certificate)) {
            return StageOutcome::Error;
        }
        if (services.opts.verbose) {
            trust::errs() << "info: wrote proof certificate: " << *req.certificatePath << "\n";
        }
    }

    if (!report.allProved()) {
        // Гейт: непроверенное обязательство — ошибка, а не тихий пропуск.
        std::size_t unproven = 0;
        for (std::size_t i = 0; i < obligations.size(); ++i) {
            const verify::ProofStatus status = report.results[i].status;
            if (status == verify::ProofStatus::Proved) {
                continue;
            }
            ++unproven;
            const std::string reason = obligations[i].envelope.value_or(std::string(verify::proofStatusId(status)));
            services.services.diag().report(Severity::Error, obligations[i].site, "--verify=all: obligation not proved ({})", reason);
        }
        trust::errs() << "error: --verify=all: " << unproven << " obligation(s) not proved (" << solver_result_note << ")\n";
        return StageOutcome::Error;
    }
    return obligations.empty() ? StageOutcome::NoWork : StageOutcome::Done;
}

} // namespace backend
} // namespace trust
