// src/verify/report.cpp
#include "verify/proof_backend.hpp"

#include "utils/error.hpp"

#include <utility>

namespace trust {
namespace verify {

bool ProofReport::allProved() const noexcept {
    // Envelope/Unknown — НЕ доказано: гейт `--verify=all` обязан их отклонить.
    for (const ProofResult& r : results) {
        if (r.status != ProofStatus::Proved) {
            return false;
        }
    }
    return true;
}

ProofReport makeProofReport(std::string_view tool, std::string_view version, const std::vector<Obligation>& obligations, std::vector<ProofResult> results) {
    // Число статусов обязано совпасть с числом обязательств: иначе результат бэкенда неполон и
    // отчёт «всё доказано» был бы ложью.
    EXPECT(results.size() == obligations.size() && "proof backend must return one status per obligation");
    for (std::size_t i = 0; i < results.size(); ++i) {
        results[i].index = i;
        // Обязательство вне области проверки НЕ может быть объявлено доказанным.
        if (obligations[i].envelope.has_value() && results[i].status != ProofStatus::Envelope) {
            EXPECT(false && "outside-envelope obligation must be reported as Envelope");
        }
    }
    ProofReport report;
    report.tool = std::string(tool);
    report.version = std::string(version);
    report.results = std::move(results);
    return report;
}

} // namespace verify
} // namespace trust
