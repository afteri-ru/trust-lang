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

    // Hover over `x` in `x += 5;`. Номер строки зависит от prepended-инклудов
    // рантайма (напр. `#include <cstdint>`), поэтому ищем её динамически.
    std::ifstream ifs(cppPath);
    std::string cppText((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    int x5Line = -1;
    {
        int n = 0;
        size_t p = 0;
        while (p <= cppText.size()) {
            auto nl = cppText.find('\n', p);
            std::string line = (nl == std::string::npos) ? cppText.substr(p) : cppText.substr(p, nl - p);
            if (line.find("x += 5") != std::string::npos) {
                x5Line = n;
            }
            if (nl == std::string::npos) {
                break;
            }
            p = nl + 1;
            ++n;
        }
    }
    ASSERT_GE(x5Line, 0) << "x += 5 line not found in cppt:\n" << cppText;

    json hoverReq = {{"jsonrpc", "2.0"},
                     {"id", 42},
                     {"method", "textDocument/hover"},
                     {"params", {{"textDocument", {{"uri", cppUri}}}, {"position", {{"line", x5Line}, {"character", 0}}}}}};
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
        if (item.is_string() && std::string(item).find("← Trust: x += 5") != std::string::npos) {
            foundReverseLink = true;
        }
    }
    EXPECT_TRUE(foundReverseLink) << resp.dump();
}

// ═══════════════════════════════════════════════════════════════
// handleHover (C++ file) — reverse navigation via a name mapping,
// based on examples/rational.src. Hover over `c_fact` in the cppt
// must produce a "← Trust: c_fact" link whose #L fragment points at
// the `fact` declaration in the trust source, and the base code block
// must show that same declaration (regression: with prepended runtime
// includes the reverse statement lookup used body-aligned offsets, so
// findRangeMap returned a neighbouring statement).
// ═══════════════════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleHover_CppReverseLinkForRationalExample) {
    // Представительный вариант examples/rational.src (без гигантского @assert-литерала,
    // но с самим макросом @assert) — структура маппинга: @main, рац. литерал, @while, @assert, :StrChar.
    std::ofstream(testSrcFile) << "@main():={\n"
                                  "    fact :Rational := 1\\1;\n"
                                  "    mult := 1;\n"
                                  "    @while( mult <= 1000 ) {\n"
                                  "        fact *= mult;\n"
                                  "        mult += 1;\n"
                                  "    };\n"
                                  "    @assert( fact == 3\\4 );\n"
                                  "    print('{}', :StrChar(fact));\n"
                                  "}\n";
    openTrustFile();

    std::string cppPath = fs::absolute(fs::path(testCppDir) / "test.cppt").string();
    std::string cppUri = "file://" + cppPath;
    std::string trustUri = "file://" + testSrcFile;

    std::ifstream ifs(cppPath);
    std::string cppText((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    auto hoverAt = [&](const std::string& uri, int line, int ch) -> json {
        json h = {{"jsonrpc", "2.0"},
                  {"id", 42},
                  {"method", "textDocument/hover"},
                  {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", ch}}}}}};
        lsp->handleRequest(h);
        auto r = transport.lastResponse();
        transport.capturedOutput.clear();
        return r;
    };

    // ── 1. Обратный ховер (cpp → trust) над объявлением `c_fact = trust::Rational` ──
    int cfactLine = -1;
    {
        int n = 0;
        size_t p = 0;
        while (p <= cppText.size()) {
            auto nl = cppText.find('\n', p);
            std::string line = (nl == std::string::npos) ? cppText.substr(p) : cppText.substr(p, nl - p);
            if (line.find("c_fact = trust::Rational") != std::string::npos) {
                cfactLine = n;
            }
            if (nl == std::string::npos) {
                break;
            }
            p = nl + 1;
            ++n;
        }
    }
    ASSERT_GE(cfactLine, 0) << "c_fact declaration line not found in cppt:\n" << cppText;
    {
        json resp = hoverAt(cppUri, cfactLine, 20);
        ASSERT_TRUE(resp.contains("result")) << resp.dump();
        auto contents = resp["result"]["contents"];
        ASSERT_TRUE(contents.is_array()) << resp.dump();
        bool foundReverseLink = false;
        bool baseShowsFactDecl = false;
        for (const auto& item : contents) {
            if (!item.is_string()) {
                continue;
            }
            std::string s = item;
            if (s.find("← Trust: c_fact") != std::string::npos) {
                foundReverseLink = true;
                EXPECT_NE(s.find("#L2,5-2,9"), std::string::npos) << "bad fragment: " << s;
            }
            if (s.find("fact :Rational := 1") != std::string::npos) {
                baseShowsFactDecl = true;
            }
        }
        EXPECT_TRUE(foundReverseLink) << resp.dump();
        EXPECT_TRUE(baseShowsFactDecl) << "base hover block shows wrong statement:\n" << resp.dump();
    }

    // ── 2. Обратный ховер (cpp → trust) над сгенерированным кодом макроса @assert ──
    // Ровно одна ссылка на сам statement @assert; не падать (регрессия: getMacroDefRange
    // возвращал range за пределами source для макросов в конце DSL-файла).
    {
        int assertLine = -1;
        {
            int n = 0;
            size_t p = 0;
            while (p <= cppText.size()) {
                auto nl = cppText.find('\n', p);
                std::string line = (nl == std::string::npos) ? cppText.substr(p) : cppText.substr(p, nl - p);
                if (line.find("trust__abort__") != std::string::npos) {
                    assertLine = n;
                }
                if (nl == std::string::npos) {
                    break;
                }
                p = nl + 1;
                ++n;
            }
        }
        ASSERT_GE(assertLine, 0) << "assert line not found in cppt:\n" << cppText;
        std::string assertCpp = [&] {
            size_t p = 0;
            for (int i = 0; i < assertLine; ++i) {
                p = cppText.find('\n', p) + 1;
            }
            auto nl = cppText.find('\n', p);
            return (nl == std::string::npos) ? cppText.substr(p) : cppText.substr(p, nl - p);
        }();
        for (int ch = 0; ch < static_cast<int>(assertCpp.size()); ++ch) {
            if (!(std::isalnum(static_cast<unsigned char>(assertCpp[ch])) || assertCpp[ch] == '_')) {
                continue;
            }
            json resp = hoverAt(cppUri, assertLine, ch);
            ASSERT_TRUE(resp.contains("result")) << "hover over @assert code crashed at ch" << ch << ": " << resp.dump();
            int links = 0;
            std::string linkText;
            for (const auto& item : resp["result"]["contents"]) {
                if (item.is_string()) {
                    std::string s = item;
                    if (s.find("← Trust:") != std::string::npos) {
                        ++links;
                        linkText = s;
                    }
                }
            }
            EXPECT_LE(links, 1) << "line " << assertLine << " ch" << ch << "\n" << resp.dump();
            if (links == 1) {
                EXPECT_NE(linkText.find("@assert( fact == 3"), std::string::npos) << "line " << assertLine << " ch" << ch << "\n" << resp.dump();
            }
        }
    }

    // ── 3. Обратный ховер (cpp → trust): вложенные выражения — ровно одна ссылка ──
    {
        std::vector<std::pair<int, std::string>> targets;
        {
            int n = 0;
            size_t p = 0;
            while (p <= cppText.size()) {
                auto nl = cppText.find('\n', p);
                std::string line = (nl == std::string::npos) ? cppText.substr(p) : cppText.substr(p, nl - p);
                if (line.find("c_mult += 1") != std::string::npos || line.find("c_fact *= c_mult") != std::string::npos) {
                    targets.emplace_back(n, line);
                }
                if (nl == std::string::npos) {
                    break;
                }
                p = nl + 1;
                ++n;
            }
        }
        ASSERT_FALSE(targets.empty()) << "nested expression lines not found in cppt:\n" << cppText;
        for (const auto& [tl, lineText] : targets) {
            for (int ch = 0; ch < static_cast<int>(lineText.size()); ++ch) {
                json resp = hoverAt(cppUri, tl, ch);
                if (!resp.contains("result") || !resp["result"].contains("contents")) {
                    continue;
                }
                int links = 0;
                std::string linkText;
                for (const auto& item : resp["result"]["contents"]) {
                    if (item.is_string()) {
                        std::string s = item;
                        if (s.find("← Trust:") != std::string::npos) {
                            ++links;
                            linkText = s;
                        }
                    }
                }
                EXPECT_LE(links, 1) << "line " << tl << " ch" << ch << ": multiple reverse links\n" << resp.dump();
                if (links == 1) {
                    const std::string expected = (lineText.find("c_mult += 1") != std::string::npos) ? "mult += 1" : "fact *= mult";
                    EXPECT_NE(linkText.find(expected), std::string::npos) << "line " << tl << " ch" << ch << "\n" << resp.dump();
                }
            }
        }
    }

    // ── 4. Прямой ховер (trust → cpp) над макросами @assert/@while/print ──
    // Не должен падать и должен давать ссылку «→ C++» на раскрытый код в cppt
    // (регрессия: раньше «Macro:»-ссылка уводила в конец src-файла).
    {
        std::ifstream tfs(testSrcFile);
        std::string trustText((std::istreambuf_iterator<char>(tfs)), std::istreambuf_iterator<char>());
        int ln = 0;
        size_t p = 0;
        bool sawForwardCpp = false;
        bool sawMacroDefLink = false;
        while (p <= trustText.size()) {
            auto nl = trustText.find('\n', p);
            std::string line = (nl == std::string::npos) ? trustText.substr(p) : trustText.substr(p, nl - p);
            if (line.find("@assert") != std::string::npos || line.find("@while") != std::string::npos || line.find("print('{}'") != std::string::npos) {
                for (int ch = 0; ch < static_cast<int>(line.size()); ++ch) {
                    if (!(std::isalnum(static_cast<unsigned char>(line[ch])) || line[ch] == '@')) {
                        continue;
                    }
                    json resp = hoverAt(trustUri, ln, ch);
                    ASSERT_TRUE(resp.contains("result")) << "forward hover over macro crashed at line " << ln << " ch" << ch << ": " << resp.dump();
                    for (const auto& item : resp["result"]["contents"]) {
                        if (item.is_string() && std::string(item).find("→ C++") != std::string::npos) {
                            // Ссылка должна вести в cppt-файл, а не в конец src.
                            sawForwardCpp = true;
                            EXPECT_NE(std::string(item).find(cppUri), std::string::npos) << "macro forward link must point into cpp:\n" << resp.dump();
                        }
                        if (item.is_string() && std::string(item).find("[Macro: ") != std::string::npos) {
                            // Вторая ссылка — на определение макроса в trust/dsl.src (должна вести в него).
                            sawMacroDefLink = true;
                            EXPECT_NE(std::string(item).find("trust/dsl.src"), std::string::npos) << "macro def link must point into trust/dsl.src:\n"
                                                                                                  << resp.dump();
                        }
                    }
                }
            }
            if (nl == std::string::npos) {
                break;
            }
            p = nl + 1;
            ++ln;
        }
        EXPECT_TRUE(sawForwardCpp) << "no → C++ link produced for a macro forward hover";
        EXPECT_TRUE(sawMacroDefLink) << "no [Macro: ...] def link produced for a macro forward hover";
    }
}

// ═══════════════════════════════════════════════════
// handleDidChange — анализ по буферу, а не по диску
// ═══════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleDidChange_UsesBufferContent) {
    // На диске лежит СТАРЫЙ текст (только 1 строка) и НЕ содержит новых операций.
    std::ofstream(testSrcFile) << "msg := \"old\";\n";

    std::string fileUri = "file://" + testSrcFile;
    std::string openContent = "msg := \"hello\";\n{% printf(\"%s\", $msg); %}\n";
    json openReq = {{"jsonrpc", "2.0"},
                    {"method", "textDocument/didOpen"},
                    {"params", {{"textDocument", {{"uri", fileUri}, {"languageId", "trust"}, {"version", 1}, {"text", openContent}}}}}};
    lsp->handleNotification(openReq);
    transport.capturedOutput.clear();

    // didChange: добавляем новую строку в буфер (на диск НЕ пишем).
    std::string changedContent = "msg := \"hello\";\nmsg += \" world\";\n{% printf(\"%s\", $msg); %}\n";
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
    std::string openContent = "msg := \"hello\";\n{% printf(\"%s\", $msg); %}\n";
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

// ═══════════════════════════════════════════════════════════════
// documentLink: forward (trust→cpp) и reverse (cpp→trust)
// - trust→cpp: макросы (@assert/@while/print) и операторы ведут в cppt
//   (регрессия: раньше координаты определения макроса из "@dsl" применялись к
//   src → переход уводил в конец файла).
// - cpp→trust: строка print ведёт в print-фрагмент src, а НЕ в начало функции
//   (регрессия: маппинг всей функции перекрывал точечный и уводил к началу).
// ═══════════════════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleDocumentLink_ForwardReverseTargets) {
    std::ofstream(testSrcFile) << "@main():={\n"
                                  "    fact :Rational := 1\\1;\n"
                                  "    mult := 1;\n"
                                  "    @while( mult <= 1000 ) {\n"
                                  "        fact *= mult;\n"
                                  "        mult += 1;\n"
                                  "    };\n"
                                  "    @assert( fact == 3\\4 );\n"
                                  "    print('{}', :StrChar(fact));\n"
                                  "}\n";
    openTrustFile();
    std::string cppPath = fs::absolute(fs::path(testCppDir) / "test.cppt").string();
    std::string cppUri = "file://" + cppPath;
    std::string trustUri = "file://" + testSrcFile;
    std::ifstream ifs(cppPath);
    std::string cppText((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    std::ifstream tfs(testSrcFile);
    std::string srcText((std::istreambuf_iterator<char>(tfs)), std::istreambuf_iterator<char>());

    auto docLink = [&](const std::string& uri, int id) -> json {
        json req = {{"jsonrpc", "2.0"}, {"id", id}, {"method", "textDocument/documentLink"}, {"params", {{"textDocument", {{"uri", uri}}}}}};
        lsp->handleRequest(req);
        auto r = transport.lastResponse();
        transport.capturedOutput.clear();
        return r;
    };
    auto findLine = [&](const std::string& hay, const std::string& needle) -> int {
        int n = 0;
        size_t p = 0;
        while (p <= hay.size()) {
            auto nl = hay.find('\n', p);
            std::string l = (nl == std::string::npos) ? hay.substr(p) : hay.substr(p, nl - p);
            if (l.find(needle) != std::string::npos) {
                return n;
            }
            if (nl == std::string::npos) {
                break;
            }
            p = nl + 1;
            ++n;
        }
        return -1;
    };

    // ── Forward: trust→cpp — все цели в cppt, макросы не уводят в конец src ──
    {
        json dl = docLink(trustUri, 200);
        ASSERT_TRUE(dl.contains("result") && dl["result"].is_array());
        ASSERT_FALSE(dl["result"].empty()) << "expected forward document links";
        for (const auto& link : dl["result"]) {
            const std::string target = link["target"].get<std::string>();
            EXPECT_NE(target.find(cppUri), std::string::npos) << "forward link must target cppt: " << target;
            EXPECT_EQ(target.find(testSrcFile), std::string::npos) << "forward link must NOT target src: " << target;
        }
    }

    // ── Reverse: cpp→trust — print-строка ведёт в print-фрагмент, а не в начало функции ──
    {
        int cppPrintLine = findLine(cppText, "trust__print__");
        int srcPrintLine = findLine(srcText, "print('{}'");
        ASSERT_GE(cppPrintLine, 0) << "cpp print line not found";
        ASSERT_GE(srcPrintLine, 0) << "src print line not found";

        json dl = docLink(cppUri, 201);
        ASSERT_TRUE(dl.contains("result") && dl["result"].is_array());
        json found;
        bool have = false;
        for (const auto& link : dl["result"]) {
            int startL = link["range"]["start"]["line"].get<int>();
            int endL = link["range"]["end"]["line"].get<int>();
            if (cppPrintLine >= startL && cppPrintLine <= endL) {
                found = link;
                have = true;
                break;
            }
        }
        ASSERT_TRUE(have) << "no cppt documentLink covering print line " << cppPrintLine;
        const std::string target = found["target"].get<std::string>();
        std::string wantFragment = "#L" + std::to_string(srcPrintLine + 1) + ",";
        EXPECT_NE(target.find(wantFragment), std::string::npos) << "cpp print link must point to print fragment " << wantFragment << ": " << target;
        EXPECT_EQ(target.find("#L1,"), std::string::npos) << "cpp print link must not point to function start: " << target;
    }
}

// ═══════════════════════════════════════════════════════════════
// Ховер над макросом: ссылка на определение («Macro:») должна идти ПЕРВОЙ,
// а «→ C++» (раскрытый код, часто большой) — второй, чтобы не скрывать
// определение. Диапазон в dsl.src должен выделять ВЕСЬ макрос (начинаться
// с имени макроса, а не с тела).
// ═══════════════════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleHover_MacroDefLinkFirst_WholeRange) {
    std::ofstream(testSrcFile) << "@main():={\n"
                                  "    fact :Rational := 1\\1;\n"
                                  "    mult := 1;\n"
                                  "    @while( mult <= 1000 ) {\n"
                                  "        fact *= mult;\n"
                                  "        mult += 1;\n"
                                  "    };\n"
                                  "    @assert( fact == 3\\4 );\n"
                                  "    print('{}', :StrChar(fact));\n"
                                  "}\n";
    openTrustFile();
    std::string cppPath = fs::absolute(fs::path(testCppDir) / "test.cppt").string();
    std::string trustUri = "file://" + testSrcFile;
    std::ifstream tfs(testSrcFile);
    std::string srcText((std::istreambuf_iterator<char>(tfs)), std::istreambuf_iterator<char>());

    auto hoverAt = [&](const std::string& uri, int line, int ch, int id) -> json {
        json req = {{"jsonrpc", "2.0"},
                    {"id", id},
                    {"method", "textDocument/hover"},
                    {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", ch}}}}}};
        lsp->handleRequest(req);
        auto r = transport.lastResponse();
        transport.capturedOutput.clear();
        return r;
    };

    // Позиция @assert в src: строка 7 (0-based), символ '@' (col 4).
    json resp = hoverAt(trustUri, 7, 4, 301);
    ASSERT_TRUE(resp.contains("result") && resp["result"].contains("contents"));
    const auto& contents = resp["result"]["contents"];
    ASSERT_GE(contents.size(), 3) << "expected base + Macro + → C++ :\n" << resp.dump();

    // Индекс [0] — базовый блок кода.
    // Индекс [1] — ссылка на определение макроса (должна быть первой).
    ASSERT_TRUE(contents[1].is_string());
    const std::string first = contents[1].get<std::string>();
    EXPECT_NE(first.find("[Macro: @assert]"), std::string::npos) << "first link must be Macro def:\n" << resp.dump();
    // Ссылка на определение должна указывать в dsl.src и выделять весь макрос,
    // начиная с имени (колонка 1). Номер строки не хардкодим — проверяем формат
    // `trust/dsl.src#L<номер>,1-`, чтобы тест не зависел от содержимого dsl.src.
    const std::regex defLinkRe(R"(trust/dsl\.src#L[0-9]+,1-)");
    EXPECT_TRUE(std::regex_search(first, defLinkRe)) << "macro def range must start at macro name:\n" << first;

    // Индекс [2] — раскрытый код в cppt.
    ASSERT_TRUE(contents[2].is_string());
    const std::string second = contents[2].get<std::string>();
    EXPECT_NE(second.find("[→ C++:"), std::string::npos) << "second link must be → C++:\n" << resp.dump();
    EXPECT_NE(second.find(".cppt"), std::string::npos) << "→ C++ link must point into cppt:\n" << second;
}

// ═══════════════════════════════════════════════════
// handleCompletion
// ═══════════════════════════════════════════════════

TEST_F(TrustLspTest, HandleInitialize_ReturnsCompletionCapability) {
    json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
    lsp->handleRequest(req);

    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp["result"]["capabilities"].contains("completionProvider"));
    const auto& cp = resp["result"]["capabilities"]["completionProvider"];
    bool hasDot = false;
    for (const auto& tc : cp["triggerCharacters"]) {
        if (tc.get<std::string>() == ".") {
            hasDot = true;
        }
    }
    EXPECT_TRUE(hasDot) << "triggerCharacters must include '.'\n" << resp.dump();
}

// Вспомогательная: открыть testSrcFile с произвольным содержимым и вернуть лямбды.
namespace {
struct CompletionHelpers {
    MockLspTransport& transport;
    TrustLsp& lsp;
    const std::string& testSrcFile;

    void open(const std::string& text) {
        std::string uri = "file://" + testSrcFile;
        json req = {{"jsonrpc", "2.0"},
                    {"method", "textDocument/didOpen"},
                    {"params", {{"textDocument", {{"uri", uri}, {"languageId", "trust"}, {"version", 1}, {"text", text}}}}}};
        lsp.handleNotification(req);
        transport.capturedOutput.clear();
    }

    json at(int line, int ch, int id) {
        std::string uri = "file://" + testSrcFile;
        json req = {{"jsonrpc", "2.0"},
                    {"id", id},
                    {"method", "textDocument/completion"},
                    {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", line}, {"character", ch}}}}}};
        lsp.handleRequest(req);
        auto r = transport.lastResponse();
        transport.capturedOutput.clear();
        return r;
    }

    bool hasLabel(const json& resp, const std::string& label) {
        if (!resp.contains("result") || !resp["result"].contains("items")) {
            return false;
        }
        for (const auto& it : resp["result"]["items"]) {
            if (it.value("label", "") == label) {
                return true;
            }
        }
        return false;
    }
};
} // namespace

TEST_F(TrustLspTest, HandleCompletion_Names_FunctionPrefix) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Комментарий с префиксом (валидный документ) — курсор после 'my'.
    h.open("x := 'str';\n%myfunc(a:Int32):Int32 := a + 1;\n$value := 42;\n# my\n");

    json resp = h.at(3, 4, 101);
    ASSERT_FALSE(resp.is_discarded());
    // Префикс 'my' (без '%') → функция %myfunc.
    EXPECT_TRUE(h.hasLabel(resp, "%myfunc")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_Names_NotVisibleAfterCursor) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // x объявлена на строке 0, $value — на строке 2. Курсор на строке 1: $value ещё не виден.
    h.open("x := 'str';\n#\n$value := 42;\n");

    json resp = h.at(1, 0, 102);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "x")) << resp.dump();
    EXPECT_FALSE(h.hasLabel(resp, "$value")) << "name declared after cursor must not be visible\n" << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_Types_ColonTypes) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("x : Int32 := 5;\n# :Int\n");

    json resp = h.at(1, 5, 103);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, ":Int32")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, ":Int64")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_Macros_AtPrefix) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Комментарий с префиксом '@' — макросы берутся из @dsl.
    h.open("# @\n");

    json resp = h.at(0, 3, 104);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "@main")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "@return")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "@print")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_StrCharMethods) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("x := 'str';\n# x.\n");

    json resp = h.at(1, 4, 105);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "c_str()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "empty()")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_PrefixFilter) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Набираем x.si — member-режим, фильтрация методов по префиксу 'si'.
    h.open("x := 'str';\n# x.si\n");

    json resp = h.at(1, 6, 107);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
    EXPECT_FALSE(h.hasLabel(resp, "c_str()")) << "member completion must filter by prefix\n" << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_TypeName) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Члены есть и у самих типов: `StrChar.` → методы StrChar.
    h.open("# StrChar.\n");

    json resp = h.at(0, 10, 108);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "c_str()")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_StringLiteral) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Члены у строкового литерала: `'abc'.` → методы StrChar.
    h.open("# 'abc'.\n");

    json resp = h.at(0, 8, 109);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "length()")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_NoCrash_OnIncompleteSyntax) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Недописанный код (после '@' введён ещё один '@') не должен ронять сервер
    // и не должен возвращать ошибку — завершение работает по тексту буфера.
    h.open("x := 1;\n@@\n");

    json resp = h.at(1, 2, 110);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(resp.contains("result")) << resp.dump();
    EXPECT_FALSE(resp.contains("error")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_Macro_HasTextEdit) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // textEdit с диапазоном набранного префикса '@' — сигнатура не дублируется.
    h.open("# @\n");

    json resp = h.at(0, 3, 111);
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp["result"].contains("items"));
    for (const auto& it : resp["result"]["items"]) {
        if (it.value("label", "") != "@main") {
            continue;
        }
        ASSERT_TRUE(it.contains("textEdit")) << resp.dump();
        EXPECT_EQ(it["textEdit"]["newText"].get<std::string>(), "@main");
        // Диапазон покрывает набранный '@' (start=2, end=3 на строке 0).
        EXPECT_EQ(it["textEdit"]["range"]["start"]["character"].get<int>(), 2);
        EXPECT_EQ(it["textEdit"]["range"]["end"]["character"].get<int>(), 3);
        return;
    }
    FAIL() << "@main not found in items\n" << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_TypedVariable) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Переменная с явным типом: `x : StrChar := ...` → методы StrChar.
    h.open("x : StrChar := 'a';\n# x.\n");

    json resp = h.at(1, 4, 112);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "c_str()")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_DictFields) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Словарь: `d := (a=1, b=2,)` → поля a и b.
    h.open("d := (a=1, b=2,);\n# d.\n");

    json resp = h.at(1, 4, 113);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "a")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "b")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_NoMethodsOnPercent) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // '%' показывает свободные функции, но НЕ методы (c_str после точки).
    h.open("x := 'str';\nz := x.c_str();\n%myfunc(a:Int32):Int32 := a + 1;\n# %\n");

    json resp = h.at(3, 2, 114);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "%myfunc")) << resp.dump();
    EXPECT_FALSE(h.hasLabel(resp, "c_str")) << "method must not appear as free name\n" << resp.dump();
    EXPECT_FALSE(h.hasLabel(resp, "%c_str")) << "native method must not appear as free name\n" << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_PredefMacros) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Предопределённые @__...__ из реестра парсера (из кода, не ручной список).
    h.open("# @__\n");

    json resp = h.at(0, 5, 115);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "@__LINE__")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "@__FILE__")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "@__FUNCTION__")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_Cyrillic) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Кириллические имена (UTF-8) должны дополняться.
    h.open("привет := 'мир';\n# при\n");

    json resp = h.at(1, 5, 116);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "привет")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_AnalyzerType) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Тип объекта из анализатора (VarDecl::inferredType): b := a, где a — строка.
    // Regex-вывод не справляется (литерал 'a'), анализатор даёт StrChar.
    h.open("a := 'str';\nb := a;\n# b.\n");

    json resp = h.at(2, 4, 118);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "c_str()")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_DictType) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Именованный словарь: поля a и b раскрываются, тип литерала — Dict (не Tuple).
    h.open("d := (a=1, b=2,);\n# d.\n");

    json resp = h.at(1, 4, 117);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "a")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "b")) << resp.dump();
}

// Переменная-диапазон `a := 1..10` (тип Range<Int64>): методы берутся из абстрактного `:Range`
// (fallback — собственный дескриптор Range<Int64> пуст), включая алиас `length` (нативное `count`).
TEST_F(TrustLspTest, HandleCompletion_MemberAccess_RangeVariable) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("a := 1..10;\n# a.\n");

    json resp = h.at(1, 4, 130);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "count()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "length()")) << resp.dump(); // алиас (нативное count)
    EXPECT_TRUE(h.hasLabel(resp, "empty()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "at()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "start()")) << resp.dump();
}

// Члены самого типа `Range.` — включая алиас `length` (нативное имя `count`).
TEST_F(TrustLspTest, HandleCompletion_MemberAccess_RangeType) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("# Range.\n");

    json resp = h.at(0, 8, 131);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "count()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "length()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_UnknownDoc_EmptyItems) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Несуществующий URI — сервер должен вернуть пустой результат без ошибки.
    json req = {{"jsonrpc", "2.0"},
                {"id", 106},
                {"method", "textDocument/completion"},
                {"params", {{"textDocument", {{"uri", "file:///nonexistent.src"}}}, {"position", {{"line", 0}, {"character", 0}}}}}};
    lsp->handleRequest(req);
    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(resp.contains("result")) << resp.dump();
}

// Ни один label не должен дублироваться в ответе (dedup по seen в коллекторах).
TEST_F(TrustLspTest, HandleCompletion_NoDuplicateLabels) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("x := 'str';\n%myfunc(a:Int32):Int32 := a + 1;\n# x.\n");

    json resp = h.at(2, 4, 143);
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp["result"].contains("items"));

    std::set<std::string> labels;
    for (const auto& it : resp["result"]["items"]) {
        const std::string label = it.value("label", "");
        EXPECT_FALSE(label.empty()) << resp.dump();
        EXPECT_TRUE(labels.insert(label).second) << "duplicate completion label: " << label << "\n" << resp.dump();
    }
}

// ═══════════════════════════════════════════════════
// codeAction (quickfix по fixits)
// ═══════════════════════════════════════════════════

TEST_F(TrustLspTest, HandleInitialize_ReturnsCodeActionCapability) {
    json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}};
    lsp->handleRequest(req);

    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp["result"]["capabilities"].contains("codeActionProvider"));
    const auto& cap = resp["result"]["capabilities"]["codeActionProvider"];
    bool hasQuickfix = false;
    for (const auto& k : cap.value("codeActionKinds", json::array())) {
        if (k.get<std::string>() == "quickfix") {
            hasQuickfix = true;
        }
    }
    EXPECT_TRUE(hasQuickfix) << "codeActionKinds must include quickfix\n" << resp.dump();
}

// Клиент передаёт обратно опубликованные диагностики (включая кастомное поле "fixits").
// Для диагностики с fixit сервер должен вернуть CodeAction kind=quickfix с WorkspaceEdit.
TEST_F(TrustLspTest, HandleCodeAction_QuickfixFromFixits) {
    std::string uri = "file://" + testSrcFile;
    json fixitRange = {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 5}}}};
    json fixitDiag = {{"range", fixitRange},
                      {"severity", 1},
                      {"source", "trust-lsp"},
                      {"message", "use int32_t"},
                      {"fixits", json::array({json{{"range", fixitRange}, {"replacement", "int32_t"}}})}};

    json req = {{"jsonrpc", "2.0"},
                {"id", 200},
                {"method", "textDocument/codeAction"},
                {"params", {{"textDocument", {{"uri", uri}}}, {"range", fixitRange}, {"context", {{"diagnostics", json::array({fixitDiag})}}}}}};
    lsp->handleRequest(req);

    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp.contains("result")) << resp.dump();
    ASSERT_TRUE(resp["result"].is_array());
    ASSERT_EQ(resp["result"].size(), 1);
    const auto& action = resp["result"][0];
    EXPECT_EQ(action.value("kind", ""), "quickfix");
    EXPECT_NE(action.value("title", "").find("use int32_t"), std::string::npos);
    // WorkspaceEdit применяет fixit (замена 0..5 на "int32_t").
    const auto& changes = action["edit"]["changes"][uri];
    ASSERT_EQ(changes.size(), 1);
    EXPECT_EQ(changes[0]["newText"].get<std::string>(), "int32_t");
    EXPECT_EQ(changes[0]["range"]["start"]["character"].get<int>(), 0);
}

// Без fixits — пустой список действий (никаких quickfix).
TEST_F(TrustLspTest, HandleCodeAction_NoFixits_Empty) {
    std::string uri = "file://" + testSrcFile;
    json plainDiag = {{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}},
                      {"severity", 1},
                      {"source", "trust-lsp"},
                      {"message", "no fix"}};
    json req = {{"jsonrpc", "2.0"},
                {"id", 201},
                {"method", "textDocument/codeAction"},
                {"params",
                 {{"textDocument", {{"uri", uri}}},
                  {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}},
                  {"context", {{"diagnostics", json::array({plainDiag})}}}}}};
    lsp->handleRequest(req);

    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_TRUE(resp["result"].is_array());
    EXPECT_TRUE(resp["result"].empty()) << resp.dump();
}

// ═══════════════════════════════════════════════════════════════
// Диагностика: синтаксическая ошибка → publishDiagnostics с корректным
// диапазоном; сервер продолжает обслуживать hover/definition/completion.
// ═══════════════════════════════════════════════════════════════

TEST_F(TrustLspTest, HandleDidOpen_SyntaxError_PublishesDiagnosticWithRange) {
    // Недописанная строка `y := ;` — синтаксическая ошибка на строке 1 (0-based).
    std::string uri = "file://" + testSrcFile;
    std::string bad = "x := 1;\ny := ;\n";
    json req = {{"jsonrpc", "2.0"},
                {"method", "textDocument/didOpen"},
                {"params", {{"textDocument", {{"uri", uri}, {"languageId", "trust"}, {"version", 1}, {"text", bad}}}}}};
    lsp->handleNotification(req);

    json notify = transport.lastResponse();
    ASSERT_FALSE(notify.is_discarded());
    ASSERT_EQ(notify.value("method", ""), "textDocument/publishDiagnostics") << notify.dump();
    const auto& diags = notify["params"]["diagnostics"];
    ASSERT_TRUE(diags.is_array() && !diags.empty()) << notify.dump();

    // Хотя бы одна диагностика severity=Error (1) с валидным диапазоном,
    // указывающим на строку ошибки (1), а НЕ на начало файла, и с сообщением об ошибке.
    bool haveErrorRange = false;
    bool haveErrorMsg = false;
    for (const auto& d : diags) {
        if (d.value("severity", -1) != 1) {
            continue;
        }
        const int startLine = d["range"]["start"]["line"].get<int>();
        const int endLine = d["range"]["end"]["line"].get<int>();
        const int startCh = d["range"]["start"]["character"].get<int>();
        const int endCh = d["range"]["end"]["character"].get<int>();
        EXPECT_GE(startLine, 0) << d.dump();
        EXPECT_GE(endLine, startLine) << d.dump();
        EXPECT_GE(endCh, startCh) << d.dump();
        // Сообщение диагностики должно содержать описание ошибки.
        const std::string msg = d.value("message", "");
        EXPECT_FALSE(msg.empty()) << d.dump();
        std::string lower = msg;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower.find("error") != std::string::npos) {
            haveErrorMsg = true;
        }
        if (startLine == 1) {
            haveErrorRange = true;
        }
    }
    EXPECT_TRUE(haveErrorRange) << "expected a diagnostic on line 1 (0-based) pointing at the syntax error\n" << notify.dump();
    EXPECT_TRUE(haveErrorMsg) << "expected an error diagnostic with a descriptive message\n" << notify.dump();
}

// Синтаксическая ошибка не должна «ронять» LSP: после неё сервер продолжает
// корректно отвечать на hover/definition/completion по тому же документу.
TEST_F(TrustLspTest, HandleSyntaxError_ServerStillServesHoverDefinitionCompletion) {
    std::string uri = "file://" + testSrcFile;
    // Первая строка валидна (x := 'hi';), вторая — синтаксическая ошибка.
    std::string bad = "x := 'hi';\ny := ;\n# x.\n";
    json openReq = {{"jsonrpc", "2.0"},
                    {"method", "textDocument/didOpen"},
                    {"params", {{"textDocument", {{"uri", uri}, {"languageId", "trust"}, {"version", 1}, {"text", bad}}}}}};
    lsp->handleNotification(openReq);

    // Диагностика ошибки опубликована.
    json notify = transport.lastResponse();
    ASSERT_EQ(notify.value("method", ""), "textDocument/publishDiagnostics") << notify.dump();
    ASSERT_FALSE(notify["params"]["diagnostics"].empty());

    transport.capturedOutput.clear();

    // ── hover над `x` (строка 0) должен успешно ответить ──
    json hoverReq = {{"jsonrpc", "2.0"},
                     {"id", 301},
                     {"method", "textDocument/hover"},
                     {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", 0}, {"character", 1}}}}}};
    lsp->handleRequest(hoverReq);
    json hoverResp = transport.lastResponse();
    ASSERT_FALSE(hoverResp.is_discarded());
    ASSERT_TRUE(hoverResp.contains("result")) << hoverResp.dump();
    ASSERT_FALSE(hoverResp.contains("error")) << hoverResp.dump();

    transport.capturedOutput.clear();

    // ── completion в начале строки 2 должен вернуть items (в т.ч. x) ──
    json compReq = {{"jsonrpc", "2.0"},
                    {"id", 302},
                    {"method", "textDocument/completion"},
                    {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", 2}, {"character", 2}}}}}};
    lsp->handleRequest(compReq);
    json compResp = transport.lastResponse();
    ASSERT_FALSE(compResp.is_discarded());
    ASSERT_TRUE(compResp.contains("result")) << compResp.dump();
    ASSERT_FALSE(compResp.contains("error")) << compResp.dump();

    // ── definition по `x` (строка 0) отвечает без ошибки ──
    json defReq = {{"jsonrpc", "2.0"},
                   {"id", 303},
                   {"method", "textDocument/definition"},
                   {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", 0}, {"character", 1}}}}}};
    lsp->handleRequest(defReq);
    json defResp = transport.lastResponse();
    ASSERT_FALSE(defResp.is_discarded());
    ASSERT_TRUE(defResp.contains("result")) << defResp.dump();
    ASSERT_FALSE(defResp.contains("error")) << defResp.dump();
}

// ═══════════════════════════════════════════════════════════════
// LexerError: диапазон диагностики должен указывать на ТОТ символ, где
// произошла ошибка, а НЕ на последний символ файла (баг m_offset).
// ═══════════════════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleDidOpen_UnexpectedCharacter_PointsToActualChar) {
    std::string uri = "file://" + testSrcFile;
    // Нераспознанный контрольный символ \x01 на строке 1 (0-based). Ошибка лексера
    // должна указывать на СТРОКУ 1, а не на последний символ файла (строка 2).
    std::string bad = "x := 1;\n\x01\nz := 2;\n";
    json req = {{"jsonrpc", "2.0"},
                {"method", "textDocument/didOpen"},
                {"params", {{"textDocument", {{"uri", uri}, {"languageId", "trust"}, {"version", 1}, {"text", bad}}}}}};
    lsp->handleNotification(req);

    json notify = transport.lastResponse();
    ASSERT_EQ(notify.value("method", ""), "textDocument/publishDiagnostics") << notify.dump();

    bool foundUnexpected = false;
    for (const auto& d : notify["params"]["diagnostics"]) {
        const std::string msg = d.value("message", "");
        if (msg.find("Unexpected character") == std::string::npos) {
            continue;
        }
        foundUnexpected = true;
        // Должна указывать на строку 1 (где \x01), а НЕ на последнюю строку (2).
        const int startLine = d["range"]["start"]["line"].get<int>();
        EXPECT_EQ(startLine, 1) << "unexpected character diagnostic must point at its real line\n" << d.dump();
        // Диапазон покрывает 1 символ (не весь файл).
        const int endLine = d["range"]["end"]["line"].get<int>();
        EXPECT_EQ(endLine, 1) << d.dump();
    }
    EXPECT_TRUE(foundUnexpected) << "expected 'Unexpected character' diagnostic\n" << notify.dump();
}

// ═══════════════════════════════════════════════════════════════
// Fixit для предупреждения "creating a local variable '$extra'": bare-имя
// в локальном скоупе заменяется на сигнальное `$extra` (быстрый фикс).
// ═══════════════════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleNoSigilLocalVariable_HasFixit_AndCodeAction) {
    std::string uri = "file://" + testSrcFile;
    // `extra := 5` внутри функции — bare-имя в локальном скоупе → NoSigil warning.
    // didOpen публикует диагностики ВСЕГДА (в т.ч. warning'и) — сразу на открытии.
    std::string src = "%foo():Int32 := {\n    extra := 5;\n    extra\n};\n";
    json openReq = {{"jsonrpc", "2.0"},
                    {"method", "textDocument/didOpen"},
                    {"params", {{"textDocument", {{"uri", uri}, {"languageId", "trust"}, {"version", 1}, {"text", src}}}}}};
    lsp->handleNotification(openReq);

    json notify = transport.lastResponse();
    ASSERT_FALSE(notify.is_discarded()) << "expected publishDiagnostics on didOpen";
    ASSERT_EQ(notify.value("method", ""), "textDocument/publishDiagnostics") << notify.dump();

    // Находим диагностику NoSigil и её fixit (замена bare `extra` на `$extra`).
    json targetDiag;
    bool foundFixit = false;
    for (const auto& d : notify["params"]["diagnostics"]) {
        if (d.value("message", "").find("creating a local variable '$extra'") == std::string::npos) {
            continue;
        }
        targetDiag = d;
        // Диапазон должен быть НЕ нулевой ширины и покрывать имя `extra` (строка 1, 0-based).
        const int sl = d["range"]["start"]["line"].get<int>();
        const int sc = d["range"]["start"]["character"].get<int>();
        const int el = d["range"]["end"]["line"].get<int>();
        const int ec = d["range"]["end"]["character"].get<int>();
        EXPECT_EQ(sl, 1) << "warning must be on line 1 (0-based)\n" << d.dump();
        EXPECT_GT(ec, sc) << "warning range must be non-zero-width (highlightable)\n" << d.dump();
        EXPECT_TRUE(d.contains("data") && d["data"].contains("fixits") && d["data"]["fixits"].is_array() && !d["data"]["fixits"].empty()) << d.dump();
        for (const auto& f : d["data"]["fixits"]) {
            if (f.value("replacement", "") == "$extra") {
                foundFixit = true;
            }
        }
    }
    ASSERT_FALSE(targetDiag.is_null()) << "NoSigil diagnostic not found\n" << notify.dump();
    EXPECT_TRUE(foundFixit) << "expected fixit replacing bare name with '$extra'\n" << notify.dump();

    transport.capturedOutput.clear();

    // codeAction для этой диагностики → quickfix с WorkspaceEdit: `extra` → `$extra`.
    json actionReq = {
        {"jsonrpc", "2.0"},
        {"id", 310},
        {"method", "textDocument/codeAction"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"range", targetDiag["range"]}, {"context", {{"diagnostics", json::array({targetDiag})}}}}}};
    lsp->handleRequest(actionReq);
    json actionResp = transport.lastResponse();
    ASSERT_FALSE(actionResp.is_discarded());
    ASSERT_TRUE(actionResp.contains("result")) << actionResp.dump();

    bool foundQuickfix = false;
    for (const auto& a : actionResp["result"]) {
        if (a.value("kind", "") != "quickfix") {
            continue;
        }
        const auto& changes = a["edit"]["changes"][uri];
        for (const auto& te : changes) {
            if (te.value("newText", "") == "$extra") {
                foundQuickfix = true;
            }
        }
    }
    EXPECT_TRUE(foundQuickfix) << "expected a quickfix replacing bare name with '$extra'\n" << actionResp.dump();
}

} // namespace

// Документирующий комментарий объявления выводится в ховер (в начало Markdown-содержимого).
TEST_F(TrustLspTest, HandleHover_ShowsDocumentation) {
    std::ofstream(testSrcFile) << "/// документ для msg\nmsg := \"hello world\";\n";
    openTrustFile();

    std::string fileUri = "file://" + testSrcFile;
    json hoverReq = {{"jsonrpc", "2.0"},
                     {"id", 4},
                     {"method", "textDocument/hover"},
                     {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 1}, {"character", 1}}}}}};
    lsp->handleRequest(hoverReq);

    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp.contains("result") && resp["result"].contains("contents")) << resp.dump();
    auto contents = resp["result"]["contents"];
    ASSERT_TRUE(contents.is_array() && !contents.empty()) << resp.dump();
    // Документация (`/// документ для msg`) выводится первым элементом ховера.
    EXPECT_TRUE(contents[0].is_string());
    const std::string first = contents[0].get<std::string>();
    EXPECT_NE(first.find("документ для msg"), std::string::npos) << "hover[0]='" << first << "'";
}
