#include "transpiler/transpile_lsp_test_util.hpp"
// ============================================================================
// 1. Variable declaration (string)
// ============================================================================
TEST(TranspileLspTest, StringVarDecl) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    MapperFile cppIdx;
    runPipeline(ctx, "msg := 'hello world';\n", &cppIdx);

    // Check C++ output contains the variable declaration
    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("std::string c_msg = \"hello world\";"), std::string::npos);

    // Check source map exists
    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rTrustIdx = reader->findFileIdx("@input");
    auto mappings = reader->getTrustFileMappings(rTrustIdx);
    ASSERT_GE(mappings.size(), 1);

    // Roundtrip: trust → cpp
    auto roundtrip = reader->getMapTrustToCpp(mappings[0].from.begin);
    ASSERT_TRUE(roundtrip.has_value());
    EXPECT_EQ(roundtrip->begin, mappings[0].to.begin);

    // Roundtrip: cpp → trust
    auto roundtripBack = reader->getMapCppToTrust(mappings[0].to.begin);
    ASSERT_TRUE(roundtripBack.has_value());
    EXPECT_EQ(roundtripBack->begin, mappings[0].from.begin);

    // Генерация .cppt + .src_map в _build/test_data/StringVarDecl/
    saveAndCheckDisk(ctx, cppIdx, "StringVarDecl");
}

// ============================================================================
// 2. Embed block with printf
// ============================================================================
TEST(TranspileLspTest, EmbedWithPrintf) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    MapperFile cppIdx;
    runPipeline(ctx, "msg := 'hello world';\n{% printf(\"msg: %s\\n\", $msg); %}\n", &cppIdx);

    // Check C++ output contains variable declaration and printf
    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("std::string c_msg = \"hello world\";"), std::string::npos);
    EXPECT_NE(cpp.find("printf(\"msg: %s\\n\", c_msg);"), std::string::npos);

    // Генерация .cppt + .src_map в _build/test_data/EmbedWithPrintf/
    saveAndCheckDisk(ctx, cppIdx, "EmbedWithPrintf");

    // Check source map has 2 mappings
    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rTrustIdx = reader->findFileIdx("@input");
    auto mappings = reader->getTrustFileMappings(rTrustIdx);
    ASSERT_GE(mappings.size(), 2);
}

// ============================================================================
// 2b. Embed block expands predefined macros (@__LINE__ → номер строки)
// ============================================================================
TEST(TranspileLspTest, EmbedExpandsPredefMacro) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    MapperFile cppIdx;
    // Вставка на строке 2: @__LINE__ внутри {% %} раскрывается в 2.
    runPipeline(ctx, "x := 5;\n{% printf(\"%d\", @__LINE__); %}\n", &cppIdx);
    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("printf(\"%d\", 2);"), std::string::npos) << cpp;
    // Значение-макрос ушёл: в выводе не должно быть мангл-имени c___LINE__.
    EXPECT_EQ(cpp.find("c___LINE__"), std::string::npos) << cpp;
}

// ============================================================================
// 4. Type alias
// ============================================================================
TEST(TranspileLspTest, TypeAlias) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    MapperFile cppIdx;
    runPipeline(ctx, "MyInt ::= :Int32;\n", &cppIdx);

    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("using c_MyInt = int32_t;"), std::string::npos);

    // Генерация .cppt + .src_map в _build/test_data/TypeAlias/
    saveAndCheckDisk(ctx, cppIdx, "TypeAlias");

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rTrustIdx = reader->findFileIdx("@input");
    auto mappings = reader->getTrustFileMappings(rTrustIdx);
    ASSERT_GE(mappings.size(), 1);
}

// ============================================================================
// 5. Multiple statements
// ============================================================================
TEST(TranspileLspTest, MultipleStatements) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    MapperFile cppIdx;
    runPipeline(ctx, "a := 'hello';\nb := 'world';\n", &cppIdx);

    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("std::string c_a = \"hello\";"), std::string::npos);
    EXPECT_NE(cpp.find("std::string c_b = \"world\";"), std::string::npos);

    // Генерация .cppt + .src_map в _build/test_data/MultipleStatements/
    saveAndCheckDisk(ctx, cppIdx, "MultipleStatements");

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rTrustIdx = reader->findFileIdx("@input");
    auto mappings = reader->getTrustFileMappings(rTrustIdx);
    ASSERT_GE(mappings.size(), 2);
}

// ============================================================================
// 6. Roundtrip: trust → cpp → trust (full validation)
// ============================================================================
TEST(TranspileLspTest, RoundtripFull) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    MapperFile cppIdx;
    runPipeline(ctx, "msg := \"hello\";\n{% y = 1; %}\n", &cppIdx);

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rTrustIdx = reader->findFileIdx("@input");
    auto mappings = reader->getTrustFileMappings(rTrustIdx);
    ASSERT_GE(mappings.size(), 2);

    // For each mapping, verify roundtrip trust → cpp → trust
    for (const auto& m : mappings) {
        // trust → cpp
        auto toCpp = reader->getMapTrustToCpp(m.from.begin);
        ASSERT_TRUE(toCpp.has_value());
        EXPECT_EQ(toCpp->begin, m.to.begin);

        // cpp → trust
        auto toTrust = reader->getMapCppToTrust(m.to.begin);
        ASSERT_TRUE(toTrust.has_value());
        EXPECT_EQ(toTrust->begin, m.from.begin);
    }

    // Генерация .cppt + .src_map в _build/test_data/RoundtripFull/
    saveAndCheckDisk(ctx, cppIdx, "RoundtripFull");
}

// ============================================================================
// 7. Source map with comments (lines starting with #)
// ============================================================================
TEST(TranspileLspTest, SkipComments) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    MapperFile cppIdx;
    runPipeline(ctx, "# this is a comment\n\nx := 5;\n", &cppIdx);

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rTrustIdx = reader->findFileIdx("@input");
    auto mappings = reader->getTrustFileMappings(rTrustIdx);
    ASSERT_EQ(mappings.size(), 1);

    // Генерация .cppt + .src_map в _build/test_data/SkipComments/
    saveAndCheckDisk(ctx, cppIdx, "SkipComments");
}

// ============================================================================
// Регрессия: trust-сторона source map использует 1-based offset'ы и указывает
// на реальные имена, а не на оператор ':='. Раньше диапазоны токенов были
// 0-based, из-за чего trust-lsp неверно подсвечивал имена/операторы и давал
// неверные координаты в cppt.
// ============================================================================
TEST(TranspileLspTest, VarDeclTrustRangesAreOneBasedAndPointToName) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    MapperFile cppIdx;
    runPipeline(ctx, "msg := \"hello world\";\ncount := 5 + 3;\n", &cppIdx);

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rTrust = reader->findFileIdx("@input");
    ASSERT_FALSE(rTrust.isInvalid());

    // Statement-маппинги для `:=` начинаются с offset 1 (начало имени/строки),
    // а не с оператора (0-based раньше указывал на ` :`).
    auto mappings = reader->getTrustFileMappings(rTrust);
    ASSERT_GE(mappings.size(), 2u);
    EXPECT_EQ(mappings[0].from.begin.offset(), 1u);                   // начало `msg := ...`
    EXPECT_EQ(reader->getText(mappings[0].from).substr(0, 3), "msg"); // текст начинается с имени

    // name-маппинг для `msg`: диапазон ровно на имени (1-based [1,4) = "msg").
    const SourceMapReader::NameMap* msgMap = nullptr;
    for (const auto& nm : reader->getNameMappings()) {
        if (nm.fromName == "msg") {
            msgMap = &nm;
            break;
        }
    }
    ASSERT_NE(msgMap, nullptr);
    EXPECT_EQ(msgMap->rangeMap.from.begin.offset(), 1u);
    EXPECT_EQ(msgMap->rangeMap.from.end.offset(), 4u);
    EXPECT_EQ(reader->getText(msgMap->rangeMap.from), "msg");
}

// ============================================================================
// Макрос → транспилер → генерация .cppt/.src_map.
// Полный сценарий, отсутствовавший ранее: макрос раскрывается в парсере
// (addMacroMapping), транспилятор генерирует C++ с маппингами диапазонов
// (mapStart/mapStop) и имён (addNameMapping), а saveCppAndEmbedSourceMap
// записывает .cppt и .src_map в _build/test_data/. Проверяются все три вида
// маппинга, в том числе в .src_map, восстановленном с диска через fromMsgpack.
// ============================================================================
TEST(TranspileLspTest, MacroThenTranspileAndSave) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    // Parser::ParseText раскрывает макросы только при установленном макро-буфере.
    ctx.setMacro(std::make_shared<Macro>(ctx));

    // 1. Определение макроса парсим отдельным ParseText (как loadDslMacros в pipeline):
    //    определения макросов не должны попадать в AST пользователя.
    {
        Parser defParser(ctx);
        defParser.ParseText("@@fortytwo@@ 42 @@@@;\n");
    }

    // 2. Пользовательский код с вызовом макроса.
    const std::string src = "y := fortytwo;\n";
    MapperFile cppIdx;
    runPipeline(ctx, src, &cppIdx);

    // Пользовательский исходник находится по содержимому (вложенные Parser::ParseText
    // регистрируют дополнительные одноимённые источники "input").
    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    uint32_t userIdx = 0;
    ASSERT_TRUE(findInputBySource(reader, src, userIdx));
    ReaderFile rUser = ReaderFile::make_input(userIdx);

    // Сгенерированный C++: макрос fortytwo раскрыт в 42.
    std::string cppOut(ctx.source().output_body(cppIdx));
    EXPECT_NE(cppOut.find("int8_t c_y = 42;"), std::string::npos) << cppOut;

    // addMacroMapping: позиция вызова `fortytwo` → есть маппинг на определение.
    // Корректность диапазона (body→def) и текста покрыта в macro_test.cpp.
    size_t callIdx = src.find("fortytwo");
    ASSERT_NE(callIdx, std::string::npos);
    auto macroDef = reader->getMacroDefRange(reader->makeLoc(rUser, off1(callIdx)));
    ASSERT_TRUE(macroDef.has_value());

    // addNameMapping: переменная y → имя в сгенерированном C++.
    // Узел VarDecl после макро-раскрытия лежит в другом input-файле, поэтому
    // ищем сам маппинг по имени и запрашиваем по его собственному диапазону.
    const SourceMapReader::NameMap* yMap = nullptr;
    for (const auto& nm : reader->getNameMappings()) {
        if (nm.fromName == "y") {
            yMap = &nm;
            break;
        }
    }
    ASSERT_NE(yMap, nullptr);
    auto cppName = reader->getCppName(yMap->rangeMap.from.begin, "y");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "c_y");

    // Генерация .cppt + .src_map в _build/test_data/MacroThenTranspileAndSave/
    saveAndCheckDisk(ctx, cppIdx, "MacroThenTranspileAndSave");

    // Макро-маппинг сохраняется в .src_map на диске и восстанавливается
    namespace fs = std::filesystem;
    fs::path mapPath = fs::path(TEST_DATA_DIR) / "MacroThenTranspileAndSave" / "MacroThenTranspileAndSave.src_map";
    ASSERT_TRUE(fs::exists(mapPath));
    std::ifstream mapIfs(mapPath, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(mapIfs)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(data.empty());
    auto diskReader = SourceMapReader::fromMsgpack(data.data(), data.size());
    ASSERT_NE(diskReader, nullptr);
    // Макро-маппинг сохраняется в .src_map на диске; раскрытие макроса регистрирует
    // несколько одноимённых источников "input", поэтому ищем по всем input-файлам.
    bool macroOnDisk = false;
    for (uint32_t i = 0; i < diskReader->input_count() && !macroOnDisk; ++i) {
        auto md = diskReader->getMacroDefRange(diskReader->makeLoc(ReaderFile::make_input(i), off1(callIdx)));
        if (md.has_value()) {
            macroOnDisk = true;
        }
    }
    EXPECT_TRUE(macroOnDisk);
}
