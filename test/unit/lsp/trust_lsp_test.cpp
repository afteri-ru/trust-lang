// -----------------------------------------------------------------------
// Unit tests for TrustLsp — LSP handlers
// -----------------------------------------------------------------------

#include "lsp/trust_lsp.h"
#include "lsp/lsp_protocol.h"
#include "diag/mapper.hpp"
#include "utils/transport.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── MockTransport для LSP ──
class MockLspTransport : public trust::transport::Transport {
  public:
    std::string capturedOutput;
    std::string pendingInput;
    bool readReturned = false;

    void setNextResponse(const std::string& body) {
        pendingInput = body;
        readReturned = false;
    }

    std::string readPacket() override {
        if (readReturned || pendingInput.empty())
            return {};
        readReturned = true;
        return pendingInput;
    }

    void send(const std::string& payload) override { capturedOutput += payload; }

    // Парсит последний JSON из capturedOutput (sendLspResponse пишет без Content-Length)
    json lastResponse() const {
        if (capturedOutput.empty())
            return json();
        // Ищем начало последнего JSON-объекта
        size_t lastObj = capturedOutput.rfind("{\"jsonrpc\"");
        if (lastObj == std::string::npos) {
            // fallback: парсим весь вывод
            return json::parse(capturedOutput, nullptr, false);
        }
        return json::parse(capturedOutput.substr(lastObj), nullptr, false);
    }
};

namespace {

// ── Тестовый класс ──
class TrustLspTest : public ::testing::Test {
  protected:
    MockLspTransport transport;
    LspOptions opts;
    std::unique_ptr<TrustLsp> lsp;

    std::string tmpDir;
    std::string testSrcFile;
    std::string testCppDir;

    void SetUp() override {
        char tmpTemplate[] = "/tmp/trust_lsp_test_XXXXXX";
        char* tmp = mkdtemp(tmpTemplate);
        ASSERT_NE(tmp, nullptr);
        tmpDir = tmp;
        testCppDir = tmpDir + "/.trust";
        fs::create_directories(testCppDir);

        testSrcFile = tmpDir + "/test.src";
        std::ofstream(testSrcFile) << "create x = 42;\nprint x;\n";

        opts.projectDir = tmpDir;
        opts.tempDir = testCppDir;
        lsp = std::make_unique<TrustLsp>(transport, opts);
    }

    void TearDown() override {
        lsp.reset();
        if (!tmpDir.empty()) {
            fs::remove_all(tmpDir);
        }
    }

    // Вспомогательная: открыть trust-файл и очистить capturedOutput
    void openTrustFile() {
        std::string fileUri = "file://" + testSrcFile;
        json req = {{"jsonrpc", "2.0"},
                    {"method", "textDocument/didOpen"},
                    {"params", {{"textDocument", {{"uri", fileUri}, {"languageId", "trust"}, {"version", 1}, {"text", ""}}}}}};
        lsp->handleNotification(req);
        transport.capturedOutput.clear();
    }
};

// ═══════════════════════════════════════════════════
// handleInitialize
// ═══════════════════════════════════════════════════

TEST_F(TrustLspTest, HandleInitialize_ReturnsCapabilities) {
    json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
    lsp->handleRequest(req);

    ASSERT_FALSE(transport.capturedOutput.empty());
    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());

    EXPECT_EQ(resp["id"], 1);
    EXPECT_TRUE(resp.contains("result"));
    EXPECT_TRUE(resp["result"]["capabilities"]["definitionProvider"].get<bool>());
    EXPECT_TRUE(resp["result"]["capabilities"]["hoverProvider"].get<bool>());
    EXPECT_TRUE(resp["result"]["capabilities"]["textDocumentSync"].get<int>() == 1);
}

// ═══════════════════════════════════════════════════
// handleDidOpen — trust file
// ═══════════════════════════════════════════════════

TEST_F(TrustLspTest, HandleDidOpen_TrustFile_Transpiles) {
    std::string fileUri = "file://" + testSrcFile;
    json req = {{"jsonrpc", "2.0"},
                {"method", "textDocument/didOpen"},
                {"params", {{"textDocument", {{"uri", fileUri}, {"languageId", "trust"}, {"version", 1}, {"text", ""}}}}}};
    lsp->handleNotification(req);

    std::string expectedCpp = testCppDir + "/test.cppt";
    EXPECT_TRUE(fs::exists(expectedCpp));
}

TEST_F(TrustLspTest, HandleDidOpen_NonTrustFile_Ignored) {
    std::string fileUri = "file://" + tmpDir + "/main.cpp";
    json req = {{"jsonrpc", "2.0"},
                {"method", "textDocument/didOpen"},
                {"params", {{"textDocument", {{"uri", fileUri}, {"languageId", "cpp"}, {"version", 1}, {"text", ""}}}}}};
    lsp->handleNotification(req);

    EXPECT_TRUE(lsp->isRunning());
}

// ═══════════════════════════════════════════════════
// handleDefinition — found / not found
// ═══════════════════════════════════════════════════

TEST_F(TrustLspTest, HandleDefinition_Found) {
    openTrustFile();

    std::string fileUri = "file://" + testSrcFile;
    json defReq = {{"jsonrpc", "2.0"},
                   {"id", 2},
                   {"method", "textDocument/definition"},
                   {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 0}, {"character", 0}}}}}};
    lsp->handleRequest(defReq);

    ASSERT_FALSE(transport.capturedOutput.empty());
    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());

    EXPECT_EQ(resp["id"], 2);
    EXPECT_TRUE(resp.contains("result"));
    EXPECT_FALSE(resp["result"].is_null());
}

TEST_F(TrustLspTest, HandleDefinition_NotFound_ReturnsNull) {
    std::string fileUri = "file:///nonexistent.src";
    json req = {{"jsonrpc", "2.0"},
                {"id", 3},
                {"method", "textDocument/definition"},
                {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 0}, {"character", 0}}}}}};
    lsp->handleRequest(req);

    ASSERT_FALSE(transport.capturedOutput.empty());
    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());

    EXPECT_EQ(resp["id"], 3);
    EXPECT_TRUE(resp["result"].is_null());
}

// ═══════════════════════════════════════════════════
// handleHover — found with correct link format
// ═══════════════════════════════════════════════════

TEST_F(TrustLspTest, HandleHover_ReturnsContents) {
    openTrustFile();

    std::string fileUri = "file://" + testSrcFile;
    // Позиция (0,7) — символ 'x' в "create x = 42;"
    json hoverReq = {{"jsonrpc", "2.0"},
                     {"id", 4},
                     {"method", "textDocument/hover"},
                     {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 0}, {"character", 7}}}}}};
    lsp->handleRequest(hoverReq);

    ASSERT_FALSE(transport.capturedOutput.empty());
    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());

    EXPECT_EQ(resp["id"], 4);
    ASSERT_TRUE(resp.contains("result"));
    ASSERT_TRUE(resp["result"].contains("contents"));
    auto contents = resp["result"]["contents"];
    ASSERT_TRUE(contents.is_array());
    ASSERT_GE(contents.size(), 2) << "Expected at least 2 elements (code block + link)";

    // Базовый блок — код с подсветкой
    EXPECT_TRUE(contents[0].is_string());
    EXPECT_TRUE(contents[0].get<std::string>().rfind("```", 0) == 0);

    // Markdown link: [→ C++: x](file:///...)
    ASSERT_TRUE(contents[1].is_string());
    std::string text = contents[1].get<std::string>();
    std::cerr << "  DEBUG HoverInfo[1]: " << text << std::endl;

    EXPECT_TRUE(text.rfind("[→ C++: ", 0) == 0) << "Expected '[→ C++: ' prefix, got: " << text;
    EXPECT_NE(text.find("]("), std::string::npos) << "Should contain '](', got: " << text;
    EXPECT_NE(text.find("file://"), std::string::npos) << "Should contain file:// URI, got: " << text;
    EXPECT_TRUE(text.back() == ')') << "Should end with ')', got: " << text;
    EXPECT_EQ(text.find("Follow link"), std::string::npos) << "Should not contain 'Follow link': " << text;
}

// ═══════════════════════════════════════════════════
// handleDidChangeConfiguration
// ═══════════════════════════════════════════════════

TEST_F(TrustLspTest, HandleDidChangeConfiguration_UpdatesTempDir) {
    json req = {{"jsonrpc", "2.0"}, {"method", "workspace/didChangeConfiguration"}, {"params", {{"settings", {{"trust", {{"tempDir", "/new/temp"}}}}}}}};
    lsp->handleNotification(req);

    EXPECT_TRUE(lsp->isRunning());
}

// ═══════════════════════════════════════════════════
// handleShutdown — clears cache
// ═══════════════════════════════════════════════════

TEST_F(TrustLspTest, HandleShutdown_ClearsCache) {
    openTrustFile();

    json shutdownReq = {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "shutdown"}};
    lsp->handleRequest(shutdownReq);

    transport.capturedOutput.clear();

    // После shutdown запрашиваем definition для .src файла, которого нет на диске
    // (auto-recovery не сможет его транспилировать, и вернёт null)
    std::string nonExistentFile = tmpDir + "/nonexistent.src";
    std::string fileUri = "file://" + nonExistentFile;
    json defReq = {{"jsonrpc", "2.0"},
                   {"id", 6},
                   {"method", "textDocument/definition"},
                   {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 0}, {"character", 0}}}}}};
    lsp->handleRequest(defReq);

    ASSERT_FALSE(transport.capturedOutput.empty());
    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());

    EXPECT_EQ(resp["id"], 6);
    EXPECT_TRUE(resp["result"].is_null()) << "cache should be empty after shutdown";
}

} // namespace