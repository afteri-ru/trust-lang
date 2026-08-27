#ifndef TRUST_LSP_TEST_FIXTURE_HPP
#define TRUST_LSP_TEST_FIXTURE_HPP
// Shared fixtures for TrustLsp unit tests
// (lsp_handler_test.cpp, lsp_hover_test.cpp, lsp_completion_test.cpp, lsp_codeaction_test.cpp).
#include "lsp/trust_lsp.h"
#include "lsp/lsp_protocol.h"
#include "diag/mapper.hpp"
#include "utils/transport.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

// -- MockTransport для LSP --
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
        if (readReturned || pendingInput.empty()) {
            return {};
        }
        readReturned = true;
        return pendingInput;
    }

    void send(const std::string& payload) override { capturedOutput += payload; }

    // Парсит последний JSON из capturedOutput (sendLspResponse/notify пишут без Content-Length,
    // объекты конкатенируются). Возвращает последний полный валидный объект.
    json lastResponse() const {
        if (capturedOutput.empty()) {
            return json();
        }
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

// -- Тестовый класс --
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
        std::ofstream(testSrcFile) << "msg := \"hello world\";\n{% printf(\"msg: %s\\n\", $msg); %}\n";

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

#endif // TRUST_LSP_TEST_FIXTURE_HPP
