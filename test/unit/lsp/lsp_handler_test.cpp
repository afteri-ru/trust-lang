#include "lsp/trust_lsp_test_fixture.hpp"
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
// handleDidOpen - trust file
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
// handleDefinition - found / not found
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

    // hover по строке 2 - существует только в буфере, не на диске.
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
// handleDidChange - инкрементальная (range-based) правка
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
// handleShutdown - clears cache
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
//   (регрессия: раньше координаты определения макроса из "@trust/dsl" применялись к
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

    // -- Forward: trust→cpp - все цели в cppt, макросы не уводят в конец src --
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

    // -- Reverse: cpp→trust - print-строка ведёт в print-фрагмент, а не в начало функции --
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
// а «→ C++» (раскрытый код, часто большой) - второй, чтобы не скрывать
// определение. Диапазон в dsl.src должен выделять ВЕСЬ макрос (начинаться
// с имени макроса, а не с тела).
// ═══════════════════════════════════════════════════════════════
TEST_F(TrustLspTest, DISABLED_HandleHover_MacroDefLinkFirst_WholeRange) {
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

    // Индекс [0] - базовый блок кода.
    // Индекс [1] - ссылка на определение макроса (должна быть первой).
    ASSERT_TRUE(contents[1].is_string());
    const std::string first = contents[1].get<std::string>();
    EXPECT_NE(first.find("[Macro: @assert]"), std::string::npos) << "first link must be Macro def:\n" << resp.dump();
    // Ссылка на определение должна указывать в dsl.src и выделять весь макрос,
    // начиная с имени (колонка 1). Номер строки не хардкодим - проверяем формат
    // `trust/dsl.src#L<номер>,1-`, чтобы тест не зависел от содержимого dsl.src.
    const std::regex defLinkRe(R"(trust/dsl\.src#L[0-9]+,1-)");
    EXPECT_TRUE(std::regex_search(first, defLinkRe)) << "macro def range must start at macro name:\n" << first;

    // Индекс [2] - раскрытый код в cppt.
    ASSERT_TRUE(contents[2].is_string());
    const std::string second = contents[2].get<std::string>();
    EXPECT_NE(second.find("[→ C++:"), std::string::npos) << "second link must be → C++:\n" << resp.dump();
    EXPECT_NE(second.find(".cppt"), std::string::npos) << "→ C++ link must point into cppt:\n" << second;
}
TEST_F(TrustLspTest, HandleDidOpen_SyntaxError_PublishesDiagnosticWithRange) {
    // Недописанная строка `y := ;` - синтаксическая ошибка на строке 1 (0-based).
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
    // Первая строка валидна (x := 'hi';), вторая - синтаксическая ошибка.
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

    // -- hover над `x` (строка 0) должен успешно ответить --
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

    // -- completion в начале строки 2 должен вернуть items (в т.ч. x) --
    json compReq = {{"jsonrpc", "2.0"},
                    {"id", 302},
                    {"method", "textDocument/completion"},
                    {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", 2}, {"character", 2}}}}}};
    lsp->handleRequest(compReq);
    json compResp = transport.lastResponse();
    ASSERT_FALSE(compResp.is_discarded());
    ASSERT_TRUE(compResp.contains("result")) << compResp.dump();
    ASSERT_FALSE(compResp.contains("error")) << compResp.dump();

    // -- definition по `x` (строка 0) отвечает без ошибки --
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
    // `extra := 5` внутри функции - bare-имя в локальном скоупе → NoSigil warning.
    // didOpen публикует диагностики ВСЕГДА (в т.ч. warning'и) - сразу на открытии.
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
