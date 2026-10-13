#include "verify/certificate.hpp"
#include "verify/model/obligation.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace {

TEST(VerifyCertificate, ByteStableAndDeterministic) {
    std::vector<verify::CertificateEntry> entries;
    verify::Obligation proved;
    proved.kind = verify::ObligationKind::Contract;
    proved.goal = "postcondition";
    entries.push_back({proved, verify::ProofStatus::Proved});

    verify::Obligation envelope;
    envelope.kind = verify::ObligationKind::Invariant;
    envelope.envelope = "loop without invariant";
    entries.push_back({envelope, verify::ProofStatus::Envelope});

    const std::string first = verify::composeCertificate("abc123", "smt", "z3", entries);
    const std::string second = verify::composeCertificate("abc123", "smt", "z3", entries);
    EXPECT_EQ(first, second);
    EXPECT_NE(first.find("\"format\": \"trustcert/1\""), std::string::npos);
    EXPECT_NE(first.find("\"status\": \"proved\""), std::string::npos);
    EXPECT_NE(first.find("\"status\": \"envelope\""), std::string::npos);
    EXPECT_NE(first.find("\"envelope\": \"loop without invariant\""), std::string::npos);
}

TEST(VerifyCertificate, EscapesStrings) {
    std::vector<verify::CertificateEntry> entries;
    verify::Obligation obligation;
    obligation.envelope = "quote \" and \\ slash";
    entries.push_back({obligation, verify::ProofStatus::Envelope});
    const std::string json = verify::composeCertificate("hash", "smt", "none", entries);
    EXPECT_NE(json.find("quote \\\" and \\\\ slash"), std::string::npos);
}

TEST(VerifyObligationIds, StableKindAndStatusIds) {
    using verify::ObligationKind;
    using verify::ProofStatus;
    EXPECT_EQ(verify::obligationKindId(ObligationKind::Overflow), std::string_view("overflow"));
    EXPECT_EQ(verify::obligationKindId(ObligationKind::DivZero), std::string_view("divzero"));
    EXPECT_EQ(verify::obligationKindId(ObligationKind::Bounds), std::string_view("bounds"));
    EXPECT_EQ(verify::obligationKindId(ObligationKind::Borrow), std::string_view("borrow"));
    EXPECT_EQ(verify::obligationKindId(ObligationKind::Contract), std::string_view("contract"));
    EXPECT_EQ(verify::obligationKindId(ObligationKind::Invariant), std::string_view("invariant"));
    EXPECT_EQ(verify::obligationKindId(ObligationKind::Stack), std::string_view("stack"));

    EXPECT_EQ(verify::proofStatusId(ProofStatus::Proved), std::string_view("proved"));
    EXPECT_EQ(verify::proofStatusId(ProofStatus::Failed), std::string_view("failed"));
    EXPECT_EQ(verify::proofStatusId(ProofStatus::Unknown), std::string_view("unknown"));
    EXPECT_EQ(verify::proofStatusId(ProofStatus::Envelope), std::string_view("envelope"));
}

TEST(VerifyObligationIds, InvalidValuesFault) {
    EXPECT_THROW((void)verify::obligationKindId(static_cast<verify::ObligationKind>(255)), std::runtime_error);
    EXPECT_THROW((void)verify::proofStatusId(static_cast<verify::ProofStatus>(255)), std::runtime_error);
}

} // namespace
} // namespace trust
