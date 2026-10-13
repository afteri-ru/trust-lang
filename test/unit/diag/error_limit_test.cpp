// test/unit/diag/error_limit_test.cpp
// Лимит ошибок принадлежит DiagnosticEngine (как clang -ferror-limit):
// после N ошибок выводится ОДНО Note-сообщение, дальнейшие не-Fatal диагностики
// подавляются; 0 = без лимита; clear() сбрасывает счётчик и признак итогового сообщения.
#include "diag/diag.hpp"
#include "session/context.hpp"
#include "utils/utils.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace trust;

namespace {

class ErrorLimitFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_ctx.diag().clear();
        m_ctx.diag().setErrorLimit(kDefaultErrorLimit);
        m_prev_err = setErrs(&m_stream);
    }

    void TearDown() override { setErrs(m_prev_err); }

    std::ostream* m_prev_err = nullptr;
    std::stringstream m_stream;
    Context m_ctx;
};

} // namespace

TEST_F(ErrorLimitFixture, DefaultLimitStopsAfterTwenty) {
    EXPECT_EQ(m_ctx.diag().errorLimit(), kDefaultErrorLimit);
    for (int i = 0; i < 25; ++i) {
        m_ctx.diag().report(Severity::Error, MapperRange{}, "error {}", i);
    }
    EXPECT_EQ(m_ctx.diag().errorCount(), static_cast<int>(kDefaultErrorLimit));
    EXPECT_TRUE(m_ctx.diag().errorLimitReached());
    ASSERT_FALSE(m_ctx.diag().diagnostics().empty());
    EXPECT_EQ(m_ctx.diag().diagnostics().back().severity, Severity::Note);
    EXPECT_NE(m_ctx.diag().diagnostics().back().message.find("too many errors emitted"), std::string::npos);
}

TEST_F(ErrorLimitFixture, ZeroDisablesLimit) {
    m_ctx.diag().setErrorLimit(0);
    for (int i = 0; i < 25; ++i) {
        m_ctx.diag().report(Severity::Error, MapperRange{}, "error {}", i);
    }
    EXPECT_EQ(m_ctx.diag().errorCount(), 25);
    EXPECT_FALSE(m_ctx.diag().errorLimitReached());
}

TEST_F(ErrorLimitFixture, CustomLimitEmitsSingleSummary) {
    m_ctx.diag().setErrorLimit(2);
    for (int i = 0; i < 5; ++i) {
        m_ctx.diag().report(Severity::Error, MapperRange{}, "error {}", i);
    }
    EXPECT_EQ(m_ctx.diag().errorCount(), 2);
    int summaries = 0;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (d.severity == Severity::Note && d.message.find("too many errors emitted") != std::string::npos) {
            ++summaries;
        }
    }
    EXPECT_EQ(summaries, 1);
}

TEST_F(ErrorLimitFixture, RaisingLimitResumesDiagnostics) {
    m_ctx.diag().setErrorLimit(2);
    for (int i = 0; i < 5; ++i) {
        m_ctx.diag().report(Severity::Error, MapperRange{}, "error {}", i);
    }
    EXPECT_EQ(m_ctx.diag().errorCount(), 2);

    // Новый лимит — новая политика: диагностики снова выводятся, итоговое сообщение
    // будет выдано при достижении нового порога.
    m_ctx.diag().setErrorLimit(5);
    for (int i = 0; i < 5; ++i) {
        m_ctx.diag().report(Severity::Error, MapperRange{}, "error {}", i);
    }
    EXPECT_EQ(m_ctx.diag().errorCount(), 5);
    EXPECT_TRUE(m_ctx.diag().errorLimitReached());
}

TEST_F(ErrorLimitFixture, ClearResetsLimitStateNotValue) {
    m_ctx.diag().setErrorLimit(2);
    m_ctx.diag().report(Severity::Error, MapperRange{}, "error");
    m_ctx.diag().report(Severity::Error, MapperRange{}, "error");
    EXPECT_TRUE(m_ctx.diag().errorLimitReached());

    m_ctx.diag().clear();
    EXPECT_EQ(m_ctx.diag().errorLimit(), 2u);
    EXPECT_FALSE(m_ctx.diag().errorLimitReached());

    m_ctx.diag().report(Severity::Error, MapperRange{}, "error");
    m_ctx.diag().report(Severity::Error, MapperRange{}, "error");
    int summaries = 0;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (d.severity == Severity::Note && d.message.find("too many errors emitted") != std::string::npos) {
            ++summaries;
        }
    }
    EXPECT_EQ(summaries, 1);
}
