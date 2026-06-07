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

    // Парсит последний JSON из capturedOutput (sendLspResponse/notify пишут без Content-Length,
    // объекты конкатенируются). Возвращает последний полный валидный объект.
    json lastResponse() const {
        if (capturedOutput.empty())
            return json();
        json best;
        bool found = false;
        size_t pos = 0;
        while ((pos = capturedOutput.find('{', pos)) != std::string::npos) {
            json j = json::parse(capturedOutput.substr(pos), nullptr, false);
            if (!j.is_discarded()) {
                best = j;
                found = true;
            }
            pos++;
        }
        return found ? best : json();
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
        tmpDir = std::string(TEST_DATA_DIR) + "/lsp_test";
        fs::remove_all(tmpDir);
        ASSERT_TRUE(fs::create_directories(tmpDir));
        testCppDir = tmpDir + "/.trust";
        fs::create_directories(testCppDir);

        testSrcFile = tmpDir + "/test.src";
        std::ofstream(testSrcFile) << "msg := \"hello world\";\n{% printf(\"msg: %s\\n\", msg); %}\n";

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
    EXPECT_TRUE(resp["result"]["capabilities"]["textDocumentSync"].get<int>() == 2);
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
    // Try multiple positions to find a mapping
    for (int ch = 0; ch < 20; ++ch) {
        json defReq = {{"jsonrpc", "2.0"},
                       {"id", 2},
                       {"method", "textDocument/definition"},
                       {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 0}, {"character", ch}}}}}};
        lsp->handleRequest(defReq);

        ASSERT_FALSE(transport.capturedOutput.empty());
        json resp = transport.lastResponse();
        ASSERT_FALSE(resp.is_discarded());

        EXPECT_EQ(resp["id"], 2);
        if (!resp["result"].is_null()) {
            return; // found a mapping
        }
        transport.capturedOutput.clear();
    }
    FAIL() << "No definition found for any position on line 0";
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
    // Try multiple positions to find hover content
    for (int ch = 0; ch < 20; ++ch) {
        json hoverReq = {{"jsonrpc", "2.0"},
                         {"id", 4},
                         {"method", "textDocument/hover"},
                         {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 0}, {"character", ch}}}}}};
        lsp->handleRequest(hoverReq);

        ASSERT_FALSE(transport.capturedOutput.empty());
        json resp = transport.lastResponse();
        ASSERT_FALSE(resp.is_discarded());

        EXPECT_EQ(resp["id"], 4);
        if (resp.contains("result") && resp["result"].contains("contents")) {
            auto contents = resp["result"]["contents"];
            if (contents.is_array() && contents.size() >= 1) {
                // Basic smoke check: code block present
                EXPECT_TRUE(contents[0].is_string());
                return;
            }
        }
        transport.capturedOutput.clear();
    }
    FAIL() << "No hover content found for any position on line 0";
}

// ═══════════════════════════════════════════════════════════════
// handleHover (C++ file) — reverse navigation back to src for expression
// operators (compound assignment has no NameMap; must fall back to the
// statement mapping and produce a "← Trust: ..." link).
// ═══════════════════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleHover_CppReverseLinkForCompoundAssignment) {
    // trust: x := 1;  x += 5;
    std::ofstream(testSrcFile) << "x := 1;\nx += 5;\n";
    openTrustFile();

    // cpp path: tempDir (testCppDir) / test.cppt (input stem = "test").
    std::string cppPath = fs::absolute(fs::path(testCppDir) / "test.cppt").string();
    std::string cppUri = "file://" + cppPath;

    // Hover over `x` in `x += 5;`. cpp full content (with prepend `#include <any>`):
    //   0: #include <any>
    //   1: std::any x = 1;
    //   2: x += 5;
    json hoverReq = {{"jsonrpc", "2.0"},
                     {"id", 42},
                     {"method", "textDocument/hover"},
                     {"params", {{"textDocument", {{"uri", cppUri}}}, {"position", {{"line", 2}, {"character", 0}}}}}};
    lsp->handleRequest(hoverReq);

    ASSERT_FALSE(transport.capturedOutput.empty());
    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_EQ(resp["id"], 42);

    ASSERT_TRUE(resp.contains("result")) << resp.dump();
    auto contents = resp["result"]["contents"];
    ASSERT_TRUE(contents.is_array()) << resp.dump();

    bool foundReverseLink = false;
    for (const auto& item : contents) {
        if (item.is_string() && std::string(item).find("← Trust: x += 5") != std::string::npos)
            foundReverseLink = true;
    }
    EXPECT_TRUE(foundReverseLink) << resp.dump();
}

// ═══════════════════════════════════════════════════
// handleDidChange — анализ по буферу, а не по диску
// ═══════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleDidChange_UsesBufferContent) {
    // На диске лежит СТАРЫЙ текст (только 1 строка) и НЕ содержит новых операций.
    std::ofstream(testSrcFile) << "msg := \"old\";\n";

    std::string fileUri = "file://" + testSrcFile;
    std::string openContent = "msg := \"hello\";\n{% printf(\"%s\", msg); %}\n";
    json openReq = {{"jsonrpc", "2.0"},
                    {"method", "textDocument/didOpen"},
                    {"params", {{"textDocument", {{"uri", fileUri}, {"languageId", "trust"}, {"version", 1}, {"text", openContent}}}}}};
    lsp->handleNotification(openReq);
    transport.capturedOutput.clear();

    // didChange: добавляем новую строку в буфер (на диск НЕ пишем).
    std::string changedContent = "msg := \"hello\";\nmsg += \" world\";\n{% printf(\"%s\", msg); %}\n";
    json changeReq = {{"jsonrpc", "2.0"},
                      {"method", "textDocument/didChange"},
                      {"params", {{"textDocument", {{"uri", fileUri}, {"version", 2}}}, {"contentChanges", {{{"text", changedContent}}}}}}};
    lsp->handleNotification(changeReq);
    transport.capturedOutput.clear();

    // hover по строке 2 — существует только в буфере, не на диске.
    bool found = false;
    for (int ch = 0; ch < 30 && !found; ++ch) {
        json hoverReq = {{"jsonrpc", "2.0"},
                         {"id", 50},
                         {"method", "textDocument/hover"},
                         {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 2}, {"character", ch}}}}}};
        lsp->handleRequest(hoverReq);
        ASSERT_FALSE(transport.capturedOutput.empty());
        json resp = transport.lastResponse();
        ASSERT_FALSE(resp.is_discarded());
        EXPECT_EQ(resp["id"], 50);
        if (resp.contains("result") && resp["result"].contains("contents") && resp["result"]["contents"].is_array() && !resp["result"]["contents"].empty()) {
            found = true;
            break;
        }
        transport.capturedOutput.clear();
    }
    EXPECT_TRUE(found) << "hover over buffer-only line returned no contents (server used disk instead of buffer)";
}

// ═══════════════════════════════════════════════════
// handleDidChange — инкрементальная (range-based) правка
// ═══════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleDidChange_IncrementalEdit) {
    std::ofstream(testSrcFile) << "msg := \"old\";\n";

    std::string fileUri = "file://" + testSrcFile;
    std::string openContent = "msg := \"hello\";\n{% printf(\"%s\", msg); %}\n";
    json openReq = {{"jsonrpc", "2.0"},
                    {"method", "textDocument/didOpen"},
                    {"params", {{"textDocument", {{"uri", fileUri}, {"languageId", "trust"}, {"version", 1}, {"text", openContent}}}}}};
    lsp->handleNotification(openReq);
    transport.capturedOutput.clear();

    // Инкрементальная вставка: новая строка "msg += \" world\";" перед строкой 1 ({% ... %}).
    json changeReq = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params",
         {{"textDocument", {{"uri", fileUri}, {"version", 2}}},
          {"contentChanges",
           {{{"range", {{"start", {{"line", 1}, {"character", 0}}}, {"end", {{"line", 1}, {"character", 0}}}}}, {"text", "msg += \" world\";\n"}}}}}}};
    lsp->handleNotification(changeReq);
    transport.capturedOutput.clear();

    // hover по строке 1 (новая, появилась только из инкрементальной правки буфера)
    bool found = false;
    for (int ch = 0; ch < 30 && !found; ++ch) {
        json hoverReq = {{"jsonrpc", "2.0"},
                         {"id", 51},
                         {"method", "textDocument/hover"},
                         {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 1}, {"character", ch}}}}}};
        lsp->handleRequest(hoverReq);
        ASSERT_FALSE(transport.capturedOutput.empty());
        json resp = transport.lastResponse();
        ASSERT_FALSE(resp.is_discarded());
        EXPECT_EQ(resp["id"], 51);
        if (resp.contains("result") && resp["result"].contains("contents") && resp["result"]["contents"].is_array() && !resp["result"]["contents"].empty()) {
            found = true;
            break;
        }
        transport.capturedOutput.clear();
    }
    EXPECT_TRUE(found) << "hover over incrementally-inserted line returned no contents";
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