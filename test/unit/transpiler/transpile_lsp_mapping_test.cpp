#include "transpiler/transpile_lsp_test_util.hpp"
// ============================================================================
// 8. Reverse name mapping: hover target is the whole name, not a shifted sub-range
// ============================================================================
// `getTrustName`/`getCppName` возвращают полный NameMap: цель hover-ссылки - весь
// диапазон имени на противоположной стороне, без сдвига по позиции курсора внутри
// имени (иначе наведение на середину многосимвольного имени даёт сдвинутый target).
TEST(TranspileLspTest, ReverseNameMappingWholeNameNotShifted) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    const std::string code = "msg := 10;\n";
    MapperFile trustIdx = ctx.source().add_source("test.src", code);
    MapperFile cppIdx = ctx.source().add_output("test.cppt");

    trust::PipelineOpts popts;
    popts.input_file = "test.src";
    trust::Pipeline pipeline(ctx, popts);
    pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rCpp = ReaderFile::make_output(0);

    // cpp `msg` in `int8_t msg = 10;` at 1-based line 2, col 9..11 (0-based char 9..11).
    // Hover on the middle char 's' (0-based char 10) must still resolve to the whole `msg`.
    auto cloc = reader->lspToLocation(rCpp, 1, 10);
    auto nm = reader->getTrustName(cloc, "c_msg");
    ASSERT_TRUE(nm.has_value());
    EXPECT_EQ(std::string(reader->getText(nm->rangeMap.from)), "msg");
}

// ============================================================================
// 6. Embed block: source-map range covers only the body (not the delimiters)
// ============================================================================
// range {% ... %} блока покрывает только содержимое между разделителями; каждый
// {% ... %} - отдельный EmbedExpr-узел со своим range.
TEST(TranspileLspTest, EmbedBlockRangeCoversOnlyBody) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    const std::string code = "{% int x = 42; %}\n";
    MapperFile trustIdx = ctx.source().add_source("test.src", code);
    MapperFile cppIdx = ctx.source().add_output("test.cppt");

    trust::PipelineOpts popts;
    popts.input_file = "test.src";
    trust::Pipeline pipeline(ctx, popts);
    pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    uint32_t uIdx = 0;
    ASSERT_TRUE(findInputBySource(reader, code, uIdx));
    ReaderFile rTrust = ReaderFile::make_input(uIdx);

    auto mappings = reader->getTrustFileMappings(rTrust);
    ASSERT_GE(mappings.size(), 1u);
    // trust-range должен быть [1:3, 1:16] = содержимое " int x = 42; " без разделителей.
    EXPECT_EQ(std::string(reader->getText(mappings[0].from)), " int x = 42; ");
    EXPECT_EQ(reader->line_column(mappings[0].from.begin).column, 3u);
}

// ============================================================================
// 6b. EMBED blocks on the SAME source line stay on one line in C++
// ============================================================================
// Транспилятор вставляет '\n' между EMBED-блоками только если они на разных строках
// исходника; соседние {% ... %} на одной строке остаются на одной строке в C++.
TEST(TranspileLspTest, SameLineEmbedBlocksJoinOnOneLine) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    const std::string code = "{% int x = 42; %}; {% printf(\"hi\"); %}";
    MapperFile trustIdx = ctx.source().add_source("test.src", code);
    MapperFile cppIdx = ctx.source().add_output("test.cppt");

    trust::PipelineOpts popts;
    popts.input_file = "test.src";
    trust::Pipeline pipeline(ctx, popts);
    pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    std::string cpp(ctx.source().output_body(cppIdx));
    // Обе части на одной строке, без перевода строки между ними.
    EXPECT_NE(cpp.find(" int x = 42;  printf(\"hi\"); "), std::string::npos) << cpp;
    EXPECT_EQ(cpp.find(" int x = 42; \n printf(\"hi\"); "), std::string::npos) << cpp;
}

// ============================================================================
// 6b2. General same-line logic: two statements on the same source line join
// ============================================================================
// Логика применяется ко ВСЕМ блокам, а не только к EMBED: если строка конца предыдущего
// узла совпадает со строкой начала следующего - между ними '\n' не вставляется.
TEST(TranspileLspTest, SameLineStatementsJoinOnOneLine) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    const std::string code = "a := 1; b := 2;\n";
    MapperFile trustIdx = ctx.source().add_source("test.src", code);
    MapperFile cppIdx = ctx.source().add_output("test.cppt");

    trust::PipelineOpts popts;
    popts.input_file = "test.src";
    trust::Pipeline pipeline(ctx, popts);
    pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    std::string cpp(ctx.source().output_body(cppIdx));
    // Оба оператора в исходнике на одной строке → в C++ на одной строке, с пробелом между.
    EXPECT_NE(cpp.find("bool c_a = 1; int8_t c_b = 2;"), std::string::npos) << cpp;
    EXPECT_EQ(cpp.find("bool c_a = 1;\nint8_t c_b = 2;"), std::string::npos) << cpp;
}

// ============================================================================
// 6b3. Function body mirrors the source layout ({ / } / statements on same lines)
// ============================================================================
// Сгенерированный C++ повторяет раскладку исходника и внутри блоков: операторы на
// одной строке исходника - на одной строке, '{'/'}' следуют строкам исходника.
TEST(TranspileLspTest, FunctionBodyProperFormatting) {
    // Одно-строчный блок: нормальное многострочное форматирование с отступами.
    {
        Context ctx(".");
        TypeRegistry types(ctx.diag(), ctx.opts());
        ctx.setTypes(&types);
        const std::string code = "%foo():Void := { a := 1; b := 2; }\n";
        MapperFile trustIdx = ctx.source().add_source("test.src", code);
        MapperFile cppIdx = ctx.source().add_output("test.cppt");
        trust::PipelineOpts popts;
        popts.input_file = "test.src";
        trust::Pipeline pipeline(ctx, popts);
        pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
        ASSERT_EQ(ctx.diag().errorCount(), 0);
        std::string cpp(ctx.source().output_body(cppIdx));
        // Операторы тела функции - с отступом, каждый на своей строке.
        EXPECT_NE(cpp.find("void foo() {\n    bool c_a = 1;\n    int8_t c_b = 2;\n}"), std::string::npos) << cpp;
    }
    // Много-строчный блок: то же нормальное форматирование с отступами.
    {
        Context ctx(".");
        TypeRegistry types(ctx.diag(), ctx.opts());
        ctx.setTypes(&types);
        const std::string code = "%foo():Void := {\n  a := 1;\n  b := 2;\n}\n";
        MapperFile trustIdx = ctx.source().add_source("test.src", code);
        MapperFile cppIdx = ctx.source().add_output("test.cppt");
        trust::PipelineOpts popts;
        popts.input_file = "test.src";
        trust::Pipeline pipeline(ctx, popts);
        pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
        ASSERT_EQ(ctx.diag().errorCount(), 0);
        std::string cpp(ctx.source().output_body(cppIdx));
        EXPECT_NE(cpp.find("void foo() {\n    bool c_a = 1;\n    int8_t c_b = 2;\n}"), std::string::npos) << cpp;
    }
}

// ============================================================================
// 6c. Multiple EMBED blocks on DIFFERENT lines: each on its own C++ line
// ============================================================================
// Блоки на разных строках исходника не конкатенируются: каждый попадает в C++ на
// отдельной строке и имеет собственный source-map range.
TEST(TranspileLspTest, MultipleEmbedBlocksSeparateLines) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    const std::string code = "{% int x = 42; %};\n{% printf(\"hi\"); %}\n";
    MapperFile trustIdx = ctx.source().add_source("test.src", code);
    MapperFile cppIdx = ctx.source().add_output("test.cppt");

    trust::PipelineOpts popts;
    popts.input_file = "test.src";
    trust::Pipeline pipeline(ctx, popts);
    pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    std::string cpp(ctx.source().output_body(cppIdx));
    // Каждый блок - на отдельной строке.
    EXPECT_NE(cpp.find(" int x = 42; \n printf(\"hi\"); "), std::string::npos) << cpp;

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    uint32_t uIdx = 0;
    ASSERT_TRUE(findInputBySource(reader, code, uIdx));
    ReaderFile rTrust = ReaderFile::make_input(uIdx);

    auto mappings = reader->getTrustFileMappings(rTrust);
    ASSERT_GE(mappings.size(), 2u);
    bool foundFirst = false, foundSecond = false;
    for (const auto& m : mappings) {
        std::string t(reader->getText(m.from));
        if (t == " int x = 42; ") {
            foundFirst = true;
        }
        if (t == " printf(\"hi\"); ") {
            foundSecond = true;
        }
    }
    EXPECT_TRUE(foundFirst) << "missing range for first embed";
    EXPECT_TRUE(foundSecond) << "missing range for second embed";
}

// ============================================================================
// 7. Compound assignment: source-map range covers the whole statement (x += 5)
// ============================================================================
// Терм `+=` несёт range только оператора; source-map должен охватывать всю строку
// от m_left до m_right, иначе trust-lsp подчёркивает только оператор.
TEST(TranspileLspTest, CompoundAssignmentRangeCoversWholeStatement) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    const std::string code = "x := 1;\nx += 5;\n";
    MapperFile trustIdx = ctx.source().add_source("test.src", code);
    MapperFile cppIdx = ctx.source().add_output("test.cppt");

    trust::PipelineOpts popts;
    popts.input_file = "test.src";
    trust::Pipeline pipeline(ctx, popts);
    pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    uint32_t uIdx = 0;
    ASSERT_TRUE(findInputBySource(reader, code, uIdx));
    ReaderFile rTrust = ReaderFile::make_input(uIdx);

    bool foundFullStmt = false;
    for (const auto& m : reader->getTrustFileMappings(rTrust)) {
        std::string trustText(reader->getText(m.from));
        EXPECT_NE(trustText, "+=") << "operator-only mapping must not survive";
        if (trustText == "x += 5") {
            foundFullStmt = true;
            // Полная строка: [2:1, 2:7] (1-based).
            EXPECT_EQ(reader->line_column(m.from.begin).line, 2u);
            EXPECT_EQ(reader->line_column(m.from.begin).column, 1u);
            EXPECT_EQ(reader->line_column(m.from.end).column, 7u);
        }
    }
    EXPECT_TRUE(foundFullStmt) << "mapping for full statement 'x += 5' not found";
}

// ============================================================================
// 8. FuncDecl: сигнатура и тело маппятся раздельно
// ============================================================================
// Диапазон statement'а функции - [имя, оператор] (НЕ тело): он маппится на сигнатуру
// C++ ("void func()"). Тело (блок m_right) маппится отдельно, чтобы скобки { } были
// видны в C++, а имя функции - отдельным name-маппингом на "func".
TEST(TranspileLspTest, FuncDeclSignatureAndBodyMappedSeparately) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    const std::string code = "%foo():Void := { x := 1;\n}\n";
    MapperFile trustIdx = ctx.source().add_source("test.src", code);
    MapperFile cppIdx = ctx.source().add_output("test.cppt");

    trust::PipelineOpts popts;
    popts.input_file = "test.src";
    trust::Pipeline pipeline(ctx, popts);
    pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("void foo()"), std::string::npos) << cpp;

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    uint32_t uIdx = 0;
    ASSERT_TRUE(findInputBySource(reader, code, uIdx));
    ReaderFile rTrust = ReaderFile::make_input(uIdx);

    // 1) Сигнатура: from = [имя, оператор] ("func():Void :="; диапазон имени m_left
    //    начинается с 'func', без '%'-префикса), to = "void func()".
    //    Оператор-only ":=" маппинга быть не должно.
    bool foundSig = false;
    for (const auto& m : reader->getTrustFileMappings(rTrust)) {
        std::string trustText(reader->getText(m.from));
        EXPECT_NE(trustText, ":=") << "operator-only mapping must not survive";
        if (trustText == "foo():Void :=") {
            foundSig = true;
            // Определение (с телом) - пользовательская C++-функция без extern "C";
            // C-линковка добавляется только forward-декларациям (нет тела).
            EXPECT_EQ(reader->getText(m.to), "void foo()");
        }
    }
    EXPECT_TRUE(foundSig) << "signature mapping '[имя, оператор]' not found";

    // 2) Имя функции: name-маппинг trust "foo" -> cpp "foo".
    bool foundName = false;
    for (const auto& nm : reader->getNameMappings()) {
        if (std::string(reader->getText(nm.rangeMap.from)) == "foo") {
            foundName = true;
            EXPECT_EQ(reader->getText(nm.rangeMap.to), "foo");
        }
    }
    EXPECT_TRUE(foundName) << "function name mapping not found";

    // 3) Тело: отдельный маппинг из блока m_right (from начинается с '{').
    bool foundBody = false;
    for (const auto& m : reader->getTrustFileMappings(rTrust)) {
        std::string trustText(reader->getText(m.from));
        if (!trustText.empty() && trustText.front() == '{') {
            foundBody = true;
        }
    }
    EXPECT_TRUE(foundBody) << "body block mapping (m_right) not found";
}

// ============================================================================
// 8. Dictionary spread operators (destructure `a,b,c := ... d` и spread-merge
//    `d []= ... e`) должны корректно мапиться на выходной .cppt.
//    Регрессия: DestructureDecl не был statement-выражением (не оборачивался
//    SemicolonStmt) и не имел собственного mapStart/mapStop → у оператора
//    раскрытия словаря отсутствовала запись в source map.
// ============================================================================
TEST(TranspileLspTest, DictionarySpreadMapping) {
    Context ctx(".");
    TypeRegistry types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    MapperFile cppIdx;
    runPipeline(ctx,
                "@main() := {\n"
                "    d := (1, 2, 3,);\n"
                "    a, b, c := ... d;\n"
                "    e := (4, 5,);\n"
                "    d []= ... e;\n"
                "};\n",
                &cppIdx);

    std::string cpp(ctx.source().output_body(cppIdx));
    // Операторы раскрытия словаря эмитятся: destructure → pop_front, spread-merge → .extend.
    EXPECT_NE(cpp.find("pop_front()"), std::string::npos);
    EXPECT_NE(cpp.find(".extend("), std::string::npos);

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rTrustIdx = reader->findFileIdx("@input");
    auto mappings = reader->getTrustFileMappings(rTrustIdx);
    // Каждый statement (d:=, destructure, e:=, append) - отдельный маппинг. До фикса
    // destructure маппинга НЕ имел (не был обёрнут SemicolonStmt) → маппингов было меньше.
    ASSERT_GE(mappings.size(), 4);

    // Для каждого маппинга (в т.ч. раскрытия словаря) trust→cpp и обратно резолвятся.
    for (const auto& m : mappings) {
        auto toCpp = reader->getMapTrustToCpp(m.from.begin);
        ASSERT_TRUE(toCpp.has_value());
        auto back = reader->getMapCppToTrust(m.to.begin);
        ASSERT_TRUE(back.has_value());
    }

    // Явно: маппинг самого оператора деструктуризации `a, b, c := ... d` существует и
    // указывает на pop_front-строки в сгенерированном C++ (весь диапазон оператора).
    bool foundDestructure = false;
    for (const auto& m : mappings) {
        std::string trustText(reader->getText(m.from));
        if (trustText.find(":= ... d") != std::string::npos) {
            foundDestructure = true;
            std::string cppText(reader->getText(m.to));
            EXPECT_NE(cppText.find("pop_front"), std::string::npos) << "dict destructure should map onto pop_front C++; got: '" << cppText << "'";
        }
    }
    EXPECT_TRUE(foundDestructure) << "dict destructuring operator has no source mapping";

    // Spread-merge `d []= ... e` мапится на .extend, и его range захватывает операнд `e`
    // (ранее range терма эллипсиса заканчивался на `...` и `e` выпадал из маппинга).
    bool foundExtend = false;
    for (const auto& m : mappings) {
        std::string trustText(reader->getText(m.from));
        if (trustText.find("[]= ... e") != std::string::npos) {
            foundExtend = true;
            std::string cppText(reader->getText(m.to));
            EXPECT_NE(cppText.find(".extend("), std::string::npos) << "dict spread-merge should map onto .extend; got: '" << cppText << "'";
        }
    }
    EXPECT_TRUE(foundExtend) << "dict spread-merge operator has no source mapping";

    // Инициализатор-словарь `e := (4, 5,)`: маппинг VarDecl покрывает весь statement
    // (ранее range терма словаря заканчивался на `(` и подсвечивался только `e`).
    bool foundVarInit = false;
    for (const auto& m : mappings) {
        std::string trustText(reader->getText(m.from));
        if (trustText.find("e := (4, 5,)") != std::string::npos) {
            foundVarInit = true;
        }
    }
    EXPECT_TRUE(foundVarInit) << "VarDecl with dict initializer does not map the full 'e := (4, 5,)' statement";
}
