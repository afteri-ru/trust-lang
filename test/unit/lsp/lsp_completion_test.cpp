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
    h.open("x := \"str\";\n%myfunc(a:Int32):Int32 := a + 1;\n$value := 42;\n# my\n");

    json resp = h.at(3, 4, 101);
    ASSERT_FALSE(resp.is_discarded());
    // Префикс 'my' (без '%') → функция %myfunc.
    EXPECT_TRUE(h.hasLabel(resp, "%myfunc")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_Names_NotVisibleAfterCursor) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // x объявлена на строке 0, $value - на строке 2. Курсор на строке 1: $value ещё не виден.
    h.open("x := \"str\";\n#\n$value := 42;\n");

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
    // Комментарий с префиксом '@' - макросы берутся из @stdlib/dsl.
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
    h.open("x := \"str\";\n# x.\n");

    json resp = h.at(1, 4, 105);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "size()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "c_str()")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "empty()")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_PrefixFilter) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Набираем x.si - member-режим, фильтрация методов по префиксу 'si'.
    h.open("x := \"str\";\n# x.si\n");

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
    // Члены у строкового литерала: `"abc".` → методы StrChar.
    h.open("# \"abc\".\n");

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
    h.open("x : StrChar := \"a\";\n# x.\n");

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
    h.open("x := \"str\";\nz := x.c_str();\n%myfunc(a:Int32):Int32 := a + 1;\n# %\n");

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
    h.open("привет := \"мир\";\n# при\n");

    json resp = h.at(1, 5, 116);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "привет")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_MemberAccess_AnalyzerType) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    // Тип объекта из анализатора (VarDecl::inferredType): b := a, где a - строка.
    // Regex-вывод не справляется (литерал "a"), анализатор даёт StrChar.
    h.open("a := \"str\";\nb := a;\n# b.\n");

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
    h.open("x := \"str\";\n%myfunc(a:Int32):Int32 := a + 1;\n# x.\n");

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

// Квалифицированное завершение (`ns::<TAB>`): предлагаются имена ТОЛЬКО этой области имён,
// вставка содержит полное имя (диапазон префикса включает `ns::`).
TEST_F(TrustLspTest, HandleCompletion_QualifiedNamespace) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("x := 1;\nns:: {\n    f():Int32 := { @return 1; };\n};\n# ns::\n");

    json resp = h.at(4, 6, 144);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "ns::f")) << resp.dump();
    // Глобальное имя `x` в этой области не предлагается.
    EXPECT_FALSE(h.hasLabel(resp, "ns::x")) << resp.dump();
}

// Импортированные имена живут в ДРУГОМ файле: позиционный фильтр (строки объявления vs
// курсор) к ним неприменим, иначе имена скрываются/показываются случайно.
TEST_F(TrustLspTest, HandleCompletion_ImportedModuleSymbols_NotFilteredByLine) {
    std::ofstream(tmpDir + "/lib.src") << "libval := 7;\n\nlibfunc(a:Int32):Int32 := a + 1;\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\lib(libfunc, libval);\n# li\n");

    json resp = h.at(1, 4, 146);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "libfunc")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "libval")) << resp.dump();
}

// S9b: private-объявления чужого модуля не видны в импортёре (функция и тип),
// а экспорт, попавший в маску сайта, — виден.
TEST_F(TrustLspTest, HandleCompletion_FilePrivateAndMaskedVisibility) {
    std::ofstream(tmpDir + "/lib.src") << "_:: {\n"
                                          "    hidden():Int32 := { @return 1; };\n"
                                          "    :Priv ::= :Struct{ x:Int32 := _; };\n"
                                          "};\n"
                                          "pub():Int32 := { @return 2; };\n"
                                          "other():Int32 := { @return 3; };\n"
                                          ":PubType ::= :Int32;\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\lib(pub, PubType);\n#\n");

    json resp = h.at(1, 1, 147);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "pub")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, ":PubType")) << "exported type inside the mask must be visible\n" << resp.dump();
    EXPECT_FALSE(h.hasLabel(resp, "hidden")) << "file-private symbol must not leak\n" << resp.dump();
    EXPECT_FALSE(h.hasLabel(resp, ":Priv")) << "file-private type must not leak\n" << resp.dump();
    EXPECT_FALSE(h.hasLabel(resp, "other")) << "export excluded by the site mask must not leak\n" << resp.dump();
}

// S9b: private-объявления ВИДНЫ в файле, который их объявляет.
TEST_F(TrustLspTest, HandleCompletion_OwnPrivateVisible) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("_:: {\n"
           "    ownpriv():Int32 := { @return 1; };\n"
           "    :OwnPriv ::= :Struct{ x:Int32 := _; };\n"
           "};\n"
           "#\n");

    json resp = h.at(4, 1, 148);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "ownpriv")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, ":OwnPriv")) << resp.dump();
}

// N5: макрос, определённый в импортированном модуле, не предлагается (макросы не экспортируются),
// а собственный макрос предлагается даже БЕЗ документирующего комментария.
TEST_F(TrustLspTest, HandleCompletion_MacrosPerModuleVisibility) {
    std::ofstream(tmpDir + "/lib.src") << "@@libmacro@@ 1 @@@@;\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("@@ownmacro@@ 2 @@@@;\n\\lib(libmacro);\n# @\n");

    json resp = h.at(2, 3, 149);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "@ownmacro")) << "own macro without doc must be offered\n" << resp.dump();
    EXPECT_FALSE(h.hasLabel(resp, "@libmacro")) << "macro of an imported module must not be offered\n" << resp.dump();
}

// S2: РЕЭКСПОРТ фасада. `\facade(f);` публикует имя ВЛАДЕЛЬЦА: идентичность и модуль
// объявления остаются за `\lib`, но видимость в LSP считается по сайту фасада (маски сайта
// применяются к имени владельца). Без проекции реэкспорта имя скрывалось бы как «модуль не
// импортирован».
TEST_F(TrustLspTest, HandleCompletion_FacadeReExportVisible) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\nreexported():Int32 := { @return 1; };\n";
    std::ofstream(tmpDir + "/facade.src") << "\\facade {\n    \\lib(reexported);\n};\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\facade(reexported);\n# re\n");

    json resp = h.at(1, 4, 150);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "reexported")) << resp.dump();
}

// S3: имя-алиас (`f :- \lib::g;`) — объявление: оно входит в export-набор фасада и должно
// присутствовать в SymbolIndex, хотя C++-функция для него не эмитится (decl остаётся у цели).
TEST_F(TrustLspTest, HandleCompletion_FunctionAliasVisible) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\ng(x:Int32):Int32 := { @return x; };\n";
    std::ofstream(tmpDir + "/facade.src") << "\\facade {\n    \\lib(g);\n    f :- \\lib::g;\n};\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\facade(f);\n# f\n");

    json resp = h.at(1, 3, 151);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "f")) << resp.dump();
}

// S4: имена из активной области поиска `... = X` (using) предлагаются КОРОТКО, хотя объявление
// лежит в другой области имён (поэтому позиционный фильтр по nameRange к ним не применяется).
TEST_F(TrustLspTest, HandleCompletion_UsingScopeShortNames) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("ns:: {\n    helper():Int32 := { @return 1; };\n};\n... = ns;\n# he\n");

    json resp = h.at(4, 4, 152);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "helper")) << resp.dump();

    // Без `using` то же имя коротко не предлагается (курсор вне блока области имён).
    h.open("ns:: {\n    helper():Int32 := { @return 1; };\n};\n# he\n");
    resp = h.at(3, 4, 153);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_FALSE(h.hasLabel(resp, "helper")) << resp.dump();
}

// S4: namespace-алиас (`al:: :- \lib::ns::;`) — qualified completion разворачивает префикс
// в область модуля-владельца и вставляет набранную форму (`al::name`).
TEST_F(TrustLspTest, HandleCompletion_NamespaceAliasPrefix) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\nns:: {\n    deepfn():Int32 := { @return 1; };\n};\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\lib();\nal:: :- \\lib::ns::;\n# al::de\n");

    json resp = h.at(2, 8, 154);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "al::deepfn")) << resp.dump();
}

// R6: вложенный namespace-алиас (`outer:: { ns:: :- \lib::sub::; };`) — longest-prefix и
// имена алиасов как namespace-кандидаты.
TEST_F(TrustLspTest, HandleCompletion_NestedNamespaceAliasPrefix) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\nsub:: {\n    deepfn():Int32 := { @return 1; };\n};\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\lib();\nouter:: {\n    ns:: :- \\lib::sub::;\n};\n# outer::ns::de\n");

    json resp = h.at(4, 15, 160);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "outer::ns::deepfn")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_NamespaceAliasName) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\nsub:: {\n    deepfn():Int32 := { @return 1; };\n};\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\lib();\nouter:: {\n    ns:: :- \\lib::sub::;\n};\n# outer::n\n");

    json resp = h.at(4, 11, 161);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "outer::ns")) << resp.dump();
}

// R4: module-алиас (`\m :- \lib;`) — completion `\m::` идёт через spelling-алиас.
TEST_F(TrustLspTest, HandleCompletion_ModuleAliasPrefix) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\ng(x:Int32):Int32 := { @return x; };\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\lib(g);\n\\m :- \\lib;\n# \\m::g\n");

    json resp = h.at(2, 7, 162);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "\\m::g")) << resp.dump();
}

// R4: экспортированный namespace-алиас фасада — `\facade::outer::ns::` разворачивается в область
// владельца (longest-prefix), вставка сохраняет набранную квалификацию.
TEST_F(TrustLspTest, HandleCompletion_FacadeNamespaceAliasPrefix) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\nsub:: {\n    deepfn():Int32 := { @return 1; };\n};\n";
    std::ofstream(tmpDir + "/facade.src") << "\\facade {\n    \\lib();\n    outer:: {\n        ns:: :- \\lib::sub::;\n    };\n};\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\facade();\n# \\facade::outer::ns::de\n");

    json resp = h.at(1, 24, 163);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "\\facade::outer::ns::deepfn")) << resp.dump();
}

TEST_F(TrustLspTest, HandleCompletion_FacadeNamespaceAliasName) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\nsub:: {\n    deepfn():Int32 := { @return 1; };\n};\n";
    std::ofstream(tmpDir + "/facade.src") << "\\facade {\n    \\lib();\n    outer:: {\n        ns:: :- \\lib::sub::;\n    };\n};\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\facade();\n# \\facade::outer::n\n");
    json resp = h.at(1, 19, 164);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "\\facade::outer::ns")) << resp.dump();
}

// S4: модульно-квалифицированный префикс (`\lib::li`): кандидаты — экспорт ЭТОГО модуля,
// вставка — с модульным spelling.
TEST_F(TrustLspTest, HandleCompletion_ModuleQualifiedPrefix) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\nlibfunc():Int32 := { @return 1; };\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\lib(libfunc);\n# \\lib::li\n");

    json resp = h.at(1, 10, 155);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "\\lib::libfunc")) << resp.dump();
}

// S4: ведущий `::` (глобальная область) — префикс не должен терять кандидатов.
TEST_F(TrustLspTest, HandleCompletion_GlobalNamespacePrefix) {
    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("ns:: {\n    globalfn():Int32 := { @return 1; };\n};\n# ::ns::glob\n");

    json resp = h.at(3, 12, 156);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "::ns::globalfn")) << resp.dump();
}

// S5: одноимённые экспорты РАЗНЫХ модулей не смешиваются: каждый вставляется квалифицированно
// (`\lib::f`) — иначе вставка короткого имени дала бы `ambiguous name` на сайте использования.
TEST_F(TrustLspTest, HandleCompletion_SameNameDifferentModulesQualified) {
    std::ofstream(tmpDir + "/lib_a.src") << "\\lib_a;\nf(x:Int32):Int32 := { @return x; };\n";
    std::ofstream(tmpDir + "/lib_b.src") << "\\lib_b;\nf(x:Int32):Int32 := { @return x; };\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\lib_a(f);\n\\lib_b(f);\n# f\n");

    json resp = h.at(2, 3, 157);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "\\lib_a::f")) << resp.dump();
    EXPECT_TRUE(h.hasLabel(resp, "\\lib_b::f")) << resp.dump();
}

// S4: тип чужого модуля в qualified-completion (`\lib::T` и `:\lib::T`).
TEST_F(TrustLspTest, HandleCompletion_ModuleQualifiedType) {
    std::ofstream(tmpDir + "/lib.src") << "\\lib;\n:T ::= :Int32;\n";

    CompletionHelpers h{transport, *lsp, testSrcFile};
    h.open("\\lib();\n# \\lib::T\n");
    json resp = h.at(1, 9, 158);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, "\\lib::T")) << resp.dump();

    h.open("\\lib();\n# :\\lib::T\n");
    resp = h.at(1, 11, 159);
    ASSERT_FALSE(resp.is_discarded());
    EXPECT_TRUE(h.hasLabel(resp, ":\\lib::T")) << resp.dump();
}
