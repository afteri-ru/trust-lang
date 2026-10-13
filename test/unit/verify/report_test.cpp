#include "verify/model/obligation.hpp"
#include "verify/proof_backend.hpp"

#include "verify/verify.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace trust {
namespace {

TEST(VerifyReport, AllProvedRequiresEveryStatusProved) {
    std::vector<verify::Obligation> obligations(2);
    std::vector<verify::ProofResult> results(2);
    results[0].status = verify::ProofStatus::Proved;
    results[1].status = verify::ProofStatus::Envelope;
    EXPECT_FALSE(verify::makeProofReport("smt", "none", obligations, results).allProved());

    results[1].status = verify::ProofStatus::Proved;
    EXPECT_TRUE(verify::makeProofReport("smt", "none", obligations, results).allProved());
}

TEST(VerifyReport, StatusCountMustMatchObligations) {
    std::vector<verify::Obligation> obligations(2);
    std::vector<verify::ProofResult> results(1);
    results[0].status = verify::ProofStatus::Proved;
    EXPECT_THROW((void)verify::makeProofReport("smt", "none", obligations, results), std::runtime_error);
}

TEST(VerifyMode, ParsesOnlyAll) {
    EXPECT_TRUE(verify::parseVerifyMode("all").has_value());
    EXPECT_FALSE(verify::parseVerifyMode("").has_value());
    EXPECT_FALSE(verify::parseVerifyMode("bogus").has_value());
}

} // namespace
} // namespace trust
