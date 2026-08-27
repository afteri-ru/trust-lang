#include "lsp/trust_lsp_test_fixture.hpp"
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
    // Комментарий с префиксом (валидный документ) - курсор после 'my'.
    h.open("x := 'str';\n%myfunc(a:Int32):Int32 := a + 1;\n$value := 42;\n# my\n");

    json resp = h.at(3, 4, 101);
    ASSERT_FALSE(resp.is_discarded());
    // Префикс 'my' (без '%') → функция %myfunc.
    EXPECT_TRUE(h.hasLabel(resp, "%myfunc")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_Names_NotVisibleAfterCursor) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // x объявлена на строке 0, $value - на строке 2. Курсор на строке 1: $value ещё не виден.
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
    // Комментарий с префиксом '@' - макросы берутся из @trust/dsl.
    h.open("# @\n");

    json resp = h.at(0, 3, 104);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "@main")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "@return")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "@print")) << resp.dump();
}

// При вводе мнемонической команды `@func` подсказка показывает документирующий
// комментарий макроса.
TEST_F(TrustLspTest, HandleCompletion_Macros_ShowDocForFunc) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("# @fu\n");

    json resp = h.at(0, 5, 111);
    ASSERT_FALSE(resp.is_discarded());
    ASSERT_TRUE(resp.contains("result") && resp["result"].contains("items")) << resp.dump();

    bool sawFunc = false;
    for (const auto& it : resp["result"]["items"]) {
        if (it.value("label", "") != "@func") {
            continue;
        }
        sawFunc = true;
        EXPECT_TRUE(it.contains("documentation") && !it["documentation"].value("value", "").empty()) << "completion @func must carry doc (MarkupContent):\n"
                                                                                                     << resp.dump();
    }
    EXPECT_TRUE(sawFunc) << "no @func in completion items:\n" << resp.dump();
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
    // Набираем x.si - member-режим, фильтрация методов по префиксу 'si'.
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
    // и не должен возвращать ошибку - завершение работает по тексту буфера.
    h.open("x := 1;\n@@\n");

    json resp = h.at(1, 2, 110);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(resp.contains("result")) << resp.dump();
    EXPECT_FALSE(resp.contains("error")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_Macro_HasTextEdit) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // textEdit с диапазоном набранного префикса '@' - сигнатура не дублируется.
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
    // Тип объекта из анализатора (VarDecl::inferredType): b := a, где a - строка.
    // Regex-вывод не справляется (литерал 'a'), анализатор даёт StrChar.
    h.open("a := 'str';\nb := a;\n# b.\n");

    json resp = h.at(2, 4, 118);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "c_str()")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_DictType) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Именованный словарь: поля a и b раскрываются, тип литерала - Dict (не Tuple).
    h.open("d := (a=1, b=2,);\n# d.\n");

    json resp = h.at(1, 4, 117);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "a")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "b")) << resp.dump();
}

// Переменная-диапазон `a := 1..10` (тип Range<Int64>): методы берутся из абстрактного `:Range`
// (fallback - собственный дескриптор Range<Int64> пуст), включая алиас `length` (нативное `count`).
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

// Члены самого типа `Range.` - включая алиас `length` (нативное имя `count`).
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
    // Несуществующий URI - сервер должен вернуть пустой результат без ошибки.
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
