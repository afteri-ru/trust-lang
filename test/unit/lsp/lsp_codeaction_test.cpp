#include "lsp/trust_lsp_test_fixture.hpp"
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

// Без fixits - пустой список действий (никаких quickfix).
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
