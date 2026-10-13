// test/unit/semantic/internal_error_policy_test.cpp
// Политика ICE анализатора: CLI (recover=false) -> Fatal + backtrace (бросает FatalError);
// LSP (recover=true) -> Error и продолжение (символы/диагностики сохраняются).
#include "semantic/core/pass_runner.hpp"
#include "session/context.hpp"
#include "diag/diag.hpp"
#include "utils/utils.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace trust;

namespace {

class InternalErrorPolicyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
        m_ctx.diag().clear();
    }

    void TearDown() override {
        setErrs(m_prev_err);
        m_ctx.diag().clear();
    }

    std::ostream* m_prev_err = nullptr;
    std::stringstream m_stream;
    Context m_ctx;
};

} // namespace

TEST_F(InternalErrorPolicyTest, CliReportsFatalAndThrows) {
    EXPECT_THROW(reportInternalError(m_ctx, /*recover=*/false, "boom"), FatalError);
    ASSERT_FALSE(m_ctx.diag().diagnostics().empty());
    const auto& d = m_ctx.diag().diagnostics().back();
    EXPECT_EQ(d.severity, Severity::Fatal);
    EXPECT_NE(d.message.find("internal compiler error"), std::string::npos);
    EXPECT_NE(d.message.find("boom"), std::string::npos);
}

TEST_F(InternalErrorPolicyTest, LspRecoversWithErrorOnly) {
    EXPECT_NO_THROW(reportInternalError(m_ctx, /*recover=*/true, "partial AST"));
    ASSERT_FALSE(m_ctx.diag().diagnostics().empty());
    const auto& d = m_ctx.diag().diagnostics().back();
    EXPECT_EQ(d.severity, Severity::Error);
    EXPECT_NE(d.message.find("internal error during semantic analysis"), std::string::npos);
    EXPECT_NE(d.message.find("partial AST"), std::string::npos);
    EXPECT_EQ(m_stream.str().find("internal compiler error"), std::string::npos);
}
