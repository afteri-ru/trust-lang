// -----------------------------------------------------------------------
// Extended unit tests for DapHandler — setBreakpoints, stackTrace, variables
// -----------------------------------------------------------------------

#include "debug/dap_handler.hpp"
#include "debug/dap_transport.h"
#include "debug/gdb_debug.h"
#include "diag/mapper.hpp"

#include "mock_transport.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// ── Тестовый класс с временной ELF для integration-тестов ──
class DapHandlerExtendedTest : public ::testing::Test {
  protected:
    MockTransport mock;
    DapOptions opts;
    std::unique_ptr<DapHandler> handler;

    std::string tmpDir;
    std::string fakeElf;

    void SetUp() override {
        // Создаём временную директорию с fake ELF
        char tmpTemplate[] = "/tmp/dap_test_XXXXXX";
        char* tmp = mkdtemp(tmpTemplate);
        ASSERT_NE(tmp, nullptr);
        tmpDir = tmp;
        fakeElf = tmpDir + "/test_binary";

        handler = std::make_unique<DapHandler>(mock, opts);
    }

    void TearDown() override {
        handler.reset();
        if (!tmpDir.empty()) {
            fs::remove_all(tmpDir);
        }
    }
};

// ═══════════════════════════════════════════════════
// handleSetBreakpoints
// ═══════════════════════════════════════════════════

TEST_F(DapHandlerExtendedTest, SetBreakpoints_NoDebug_ReturnsUnverified) {
    json req = {{"type", "request"},
                {"command", "setBreakpoints"},
                {"seq", 10},
                {"arguments", {{"source", {{"path", "/test/main.src"}}}, {"breakpoints", {{{"line", 1}, {"id", 100}}, {{"line", 2}, {"id", 101}}}}}}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("breakpoints"));
    auto bps = resp["body"]["breakpoints"];
    ASSERT_EQ(bps.size(), 2);
    // Без debug все брейкпоинты неверифицированы
    EXPECT_FALSE(bps[0]["verified"].get<bool>());
    EXPECT_FALSE(bps[1]["verified"].get<bool>());
}

TEST_F(DapHandlerExtendedTest, SetBreakpoints_CppFile_Direct) {
    json req = {{"type", "request"},
                {"command", "setBreakpoints"},
                {"seq", 11},
                {"arguments", {{"source", {{"path", "/test/main.cpp"}}}, {"breakpoints", {{{"line", 5}}}}}}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
    // Без debug — unverified, но команда обработана
    ASSERT_TRUE(resp["body"].contains("breakpoints"));
}

TEST_F(DapHandlerExtendedTest, SetBreakpoints_TrustFile_NoMapping) {
    // .src файл без m_source_reader — mapping недоступен, пробуем напрямую
    json req = {{"type", "request"},
                {"command", "setBreakpoints"},
                {"seq", 12},
                {"arguments", {{"source", {{"path", "/test/main.src"}}}, {"breakpoints", {{{"line", 10}}}}}}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("breakpoints"));
    EXPECT_FALSE(resp["body"]["breakpoints"][0]["verified"].get<bool>());
}

// ═══════════════════════════════════════════════════
// handleStackTrace
// ═══════════════════════════════════════════════════

TEST_F(DapHandlerExtendedTest, StackTrace_NoDebug_ReturnsEmpty) {
    json req = {{"type", "request"}, {"command", "stackTrace"}, {"seq", 20}, {"arguments", {{"startFrame", 0}, {"levels", 10}}}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("stackFrames"));
    EXPECT_TRUE(resp["body"]["stackFrames"].empty());
}

// ═══════════════════════════════════════════════════
// handleThreads
// ═══════════════════════════════════════════════════

TEST_F(DapHandlerExtendedTest, Threads_NoDebug_ReturnsEmpty) {
    json req = {{"type", "request"}, {"command", "threads"}, {"seq", 30}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("threads"));
    // Без debug — пустой список
    EXPECT_TRUE(resp["body"]["threads"].empty());
}

// ═══════════════════════════════════════════════════
// handleScopes
// ═══════════════════════════════════════════════════

TEST_F(DapHandlerExtendedTest, Scopes_ReturnsLocal) {
    json req = {{"type", "request"}, {"command", "scopes"}, {"seq", 40}, {"arguments", {{"frameId", 0}}}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("scopes"));
    ASSERT_EQ(resp["body"]["scopes"].size(), 1);
    EXPECT_EQ(resp["body"]["scopes"][0]["name"], "Local");
    EXPECT_EQ(resp["body"]["scopes"][0]["variablesReference"], 1);
}

// ═══════════════════════════════════════════════════
// handleVariables
// ═══════════════════════════════════════════════════

TEST_F(DapHandlerExtendedTest, Variables_NoDebug_ReturnsEmpty) {
    json req = {{"type", "request"}, {"command", "variables"}, {"seq", 50}, {"arguments", {{"variablesReference", 1}}}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("variables"));
    EXPECT_TRUE(resp["body"]["variables"].empty());
}

TEST_F(DapHandlerExtendedTest, Variables_InvalidRef_ReturnsEmpty) {
    json req = {{"type", "request"}, {"command", "variables"}, {"seq", 51}, {"arguments", {{"variablesReference", 999}}}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("variables"));
    EXPECT_TRUE(resp["body"]["variables"].empty());
}

// ═══════════════════════════════════════════════════
// handleStepIn / handleStepOut / handleNext
// ═══════════════════════════════════════════════════

TEST_F(DapHandlerExtendedTest, StepIn_WithoutDebug_ReturnsSuccess) {
    json req = {{"type", "request"}, {"command", "stepIn"}, {"seq", 60}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
}

TEST_F(DapHandlerExtendedTest, StepOut_WithoutDebug_ReturnsSuccess) {
    json req = {{"type", "request"}, {"command", "stepOut"}, {"seq", 61}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
}

TEST_F(DapHandlerExtendedTest, HandleNext_WithoutDebug_ReturnsSuccess) {
    json req = {{"type", "request"}, {"command", "next"}, {"seq", 62}};
    handler->handleRequest(req);

    ASSERT_FALSE(mock.capturedOutput.empty());
    json resp = json::parse(mock.capturedOutput);
    EXPECT_TRUE(resp["success"].get<bool>());
}

} // namespace