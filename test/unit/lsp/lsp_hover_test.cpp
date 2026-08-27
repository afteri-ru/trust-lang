#include "lsp/trust_lsp_test_fixture.hpp"
// ═══════════════════════════════════════════════════
// handleHover - found with correct link format
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
// handleHover (C++ file) - reverse navigation back to src for expression
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
// handleHover (C++ file) - reverse navigation via a name mapping,
// based on examples/rational.src. Hover over `c_fact` in the cppt
// must produce a "← Trust: c_fact" link whose #L fragment points at
// the `fact` declaration in the trust source, and the base code block
// must show that same declaration (regression: with prepended runtime
// includes the reverse statement lookup used body-aligned offsets, so
// findRangeMap returned a neighbouring statement).
// ═══════════════════════════════════════════════════════════════
TEST_F(TrustLspTest, HandleHover_CppReverseLinkForRationalExample) {
    // Представительный вариант examples/rational.src (без гигантского @assert-литерала,
    // но с самим макросом @assert) - структура маппинга: @main, рац. литерал, @while, @assert, :StrChar.
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

    // -- 1. Обратный ховер (cpp → trust) над объявлением `c_fact = trust::Rational` --
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

    // -- 2. Обратный ховер (cpp → trust) над сгенерированным кодом макроса @assert --
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

    // -- 3. Обратный ховер (cpp → trust): вложенные выражения - ровно одна ссылка --
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

    // -- 4. Прямой ховер (trust → cpp) над макросами @assert/@while/print --
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
                            // Вторая ссылка - на определение макроса в trust/dsl.src (должна вести в него).
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

// Hover по каталоговому DSL-макросу (@func): макрос не транслируется в source map,
// но док берётся из единого хранилища BuiltinCatalog::macroDocs() (см. ранний return
// в hover-обработчике).
TEST_F(TrustLspTest, HandleHover_ShowsDslMacroDoc) {
    std::ofstream(testSrcFile) << "@func foo ( x ) { }\n";
    openTrustFile();

    std::string fileUri = "file://" + testSrcFile;
    json hoverReq = {{"jsonrpc", "2.0"},
                     {"id", 4},
                     {"method", "textDocument/hover"},
                     {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 0}, {"character", 2}}}}}};
    lsp->handleRequest(hoverReq);

    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp.contains("result") && resp["result"].contains("contents")) << resp.dump();
    auto contents = resp["result"]["contents"];
    ASSERT_TRUE(contents.is_array() && !contents.empty()) << resp.dump();
    EXPECT_TRUE(contents[0].is_string());
    const std::string first = contents[0].get<std::string>();
    EXPECT_NE(first.find("@func"), std::string::npos) << "hover[0]='" << first << "'";
}

// Hover по прагма-макросу (@__OPTION_PUSH__ и др.): прагма не транслируется в source map,
// но её док должен показываться из единого хранилища BuiltinCatalog::macroDocs().
TEST_F(TrustLspTest, HandleHover_ShowsPragmaMacroDoc) {
    std::ofstream(testSrcFile) << "@__OPTION_PUSH__;\n";
    openTrustFile();

    std::string fileUri = "file://" + testSrcFile;
    json hoverReq = {{"jsonrpc", "2.0"},
                     {"id", 4},
                     {"method", "textDocument/hover"},
                     {"params", {{"textDocument", {{"uri", fileUri}}}, {"position", {{"line", 0}, {"character", 2}}}}}};
    lsp->handleRequest(hoverReq);

    json resp = transport.lastResponse();
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp.contains("result") && resp["result"].contains("contents")) << resp.dump();
    auto contents = resp["result"]["contents"];
    ASSERT_TRUE(contents.is_array() && !contents.empty()) << resp.dump();
    EXPECT_TRUE(contents[0].is_string());
    const std::string first = contents[0].get<std::string>();
    EXPECT_NE(first.find("@__OPTION_PUSH__"), std::string::npos) << "hover[0]='" << first << "'";
}
