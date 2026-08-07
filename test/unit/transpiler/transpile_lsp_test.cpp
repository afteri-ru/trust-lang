// -----------------------------------------------------------------------
// Tests for transpiler pipeline (Parser → termToAst → Semantic → CppTranspiler)
// with source map validation.
// Uses the real legacy Parser API (syntax/parser.h) + termToAst.
// -----------------------------------------------------------------------

#include "diag/context.hpp"
#include "pipeline/term_to_ast.hpp"
#include "semantic/analyzer.hpp"
#include "syntax/parser.h"
#include "syntax/term.h"
#include "transpiler/transpiler.hpp"
#include "pipeline/pipeline.hpp"
#include "diag/mapper.hpp"
#include "syntax/macro.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace trust;

// Helper: run full pipeline and return source map reader
// Note: Parser::ParseText registers the text under the given source name
// (see Parser::ParseText: m_ctx.source().add_source(sourceName, text)).
static void runPipeline(Context& ctx, std::string_view trustCode, MapperFile* outCppIdx = nullptr) {
    // 1. Parse with legacy parser (registers "input" source file)
    trust::Parser parser(ctx);
    trust::TermPtr term = parser.ParseText(trustCode);
    ASSERT_NE(term, nullptr);

    // 2. Convert Term tree to AST nodes
    auto ast_nodes = termToAst(term, ctx);
    ASSERT_FALSE(ast_nodes.empty());

    // 3. Semantic analysis
    SemanticAnalyzer analyzer(ctx);
    analyzer.analyze(ast_nodes);

    // 4. Generate C++
    MapperFile cppIdx = ctx.source().add_output("test.cpp", true);
    ASSERT_FALSE(cppIdx.isInvalid());
    CppTranspiler transpiler(ctx);
    transpiler.generateToFile(ast_nodes, cppIdx);

    if (outCppIdx)
        *outCppIdx = cppIdx;
}

// ── Хелперы для проверки записи .cppt/.src_map на диск ──

// Сохраняет транспилированный C++ + source map в _build/test_data/<testName>/
// (путь по имени юнит-теста, временные имена не используются).
// Проверяет создание .cppt и .src_map на диске и что .src_map читается
// через SourceMapReader::fromMsgpack и содержит range-маппинги.
static void saveAndCheckDisk(Context& ctx, MapperFile cppIdx, const std::string& testName) {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(TEST_DATA_DIR) / testName;
    fs::remove_all(dir);
    ASSERT_TRUE(fs::create_directories(dir));

    fs::path cpptPath = dir / (testName + ".cppt");
    ASSERT_TRUE(trust::saveCppAndEmbedSourceMap(ctx, cppIdx, cpptPath, /*verbose=*/false));

    ASSERT_TRUE(fs::exists(cpptPath)) << "missing .cppt: " << cpptPath;
    fs::path mapPath = cpptPath;
    mapPath.replace_extension(".src_map");
    ASSERT_TRUE(fs::exists(mapPath)) << "missing .src_map: " << mapPath;

    // .cppt содержит сгенерированный код и #embed-секцию source map
    std::ifstream cppIfs(cpptPath);
    std::string cpp((std::istreambuf_iterator<char>(cppIfs)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(cpp.empty());
    EXPECT_NE(cpp.find("#embed"), std::string::npos) << cpp;
    EXPECT_NE(cpp.find("__debug_trust_source_map"), std::string::npos) << cpp;

    // .src_map валиден и содержит range-маппинги (mapStart/mapStop).
    // Макро-раскрытие может регистрировать несколько одноимённых источников
    // "input", поэтому считаем маппинги по всем входным файлам.
    std::ifstream mapIfs(mapPath, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(mapIfs)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(data.empty());
    auto reader = SourceMapReader::fromMsgpack(data.data(), data.size());
    ASSERT_NE(reader, nullptr);
    size_t mappingCount = 0;
    for (uint32_t i = 0; i < reader->input_count(); ++i)
        mappingCount += reader->getTrustFileMappings(ReaderFile::make_input(i)).size();
    EXPECT_GE(mappingCount, 1u);
}

// 0-based индекс в строке → 1-based offset (используется makeLoc/getText)
static uint32_t off1(size_t idx0) {
    return static_cast<uint32_t>(idx0 + 1);
}

// Ищет input-файл с заданным содержимым. Parser::ParseText при раскрытии макросов
// регистрирует множество одноимённых источников "input", поэтому пользовательский
// код находится по содержимому, а не по индексу.
static bool findInputBySource(const SourceMapReader* reader, std::string_view src, uint32_t& outIdx) {
    for (uint32_t i = 0; i < reader->input_count(); ++i) {
        if (reader->source(ReaderFile::make_input(i)) == src) {
            outIdx = i;
            return true;
        }
    }
    return false;
}

// ============================================================================
// 1. Variable declaration (string)
// ============================================================================
TEST(TranspileLspTest, StringVarDecl) {
    Context ctx(".");
    MapperFile cppIdx;
    runPipeline(ctx, "msg := \"hello world\";\n", &cppIdx);

    // Check C++ output contains the variable declaration
    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("std::any msg = \"hello world\";"), std::string::npos);

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
    MapperFile cppIdx;
    runPipeline(ctx, "msg := \"hello world\";\n{% printf(\"msg: %s\\n\", msg); %}\n", &cppIdx);

    // Check C++ output contains variable declaration and printf
    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("std::any msg = \"hello world\";"), std::string::npos);
    EXPECT_NE(cpp.find("printf(\"msg: %s\\n\", msg);"), std::string::npos);

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
// 4. Type alias
// ============================================================================
TEST(TranspileLspTest, TypeAlias) {
    Context ctx(".");
    MapperFile cppIdx;
    runPipeline(ctx, "MyInt ::= :Int32;\n", &cppIdx);

    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("using MyInt = int32_t;"), std::string::npos);

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
    MapperFile cppIdx;
    runPipeline(ctx, "a := \"hello\";\nb := \"world\";\n", &cppIdx);

    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("std::any a = \"hello\";"), std::string::npos);
    EXPECT_NE(cpp.find("std::any b = \"world\";"), std::string::npos);

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
    for (const auto& nm : reader->getNameMappings())
        if (nm.fromName == "msg") {
            msgMap = &nm;
            break;
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
    // Parser::ParseText раскрывает макросы только при установленном макро-буфере.
    ctx.setMacro(std::make_shared<Macro>(ctx));

    // 1. Определение макроса парсим отдельным ParseText (как loadDslMacros в pipeline):
    //    определения макросов не должны попадать в AST пользователя.
    {
        Parser defParser(ctx);
        defParser.ParseText("@@fortytwo@@ := 42;\n");
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
    EXPECT_NE(cppOut.find("std::any y = 42;"), std::string::npos) << cppOut;

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
    for (const auto& nm : reader->getNameMappings())
        if (nm.fromName == "y") {
            yMap = &nm;
            break;
        }
    ASSERT_NE(yMap, nullptr);
    auto cppName = reader->getCppName(yMap->rangeMap.from.begin, "y");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "y");

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
        if (md.has_value())
            macroOnDisk = true;
    }
    EXPECT_TRUE(macroOnDisk);
}

// ============================================================================
// 8. Reverse name mapping: hover target is the whole name, not a shifted sub-range
// ============================================================================
// `getTrustName`/`getCppName` возвращают полный NameMap: цель hover-ссылки — весь
// диапазон имени на противоположной стороне, без сдвига по позиции курсора внутри
// имени (иначе наведение на середину многосимвольного имени даёт сдвинутый target).
TEST(TranspileLspTest, ReverseNameMappingWholeNameNotShifted) {
    Context ctx(".");
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

    // cpp `msg` in `std::any msg = 10;` at 1-based line 2, col 9..11 (0-based char 9..11).
    // Hover on the middle char 's' (0-based char 10) must still resolve to the whole `msg`.
    auto cloc = reader->lspToLocation(rCpp, 1, 10);
    auto nm = reader->getTrustName(cloc, "msg");
    ASSERT_TRUE(nm.has_value());
    EXPECT_EQ(std::string(reader->getText(nm->rangeMap.from)), "msg");
}

// ============================================================================
// 6. Embed block: source-map range covers only the body (not the delimiters)
// ============================================================================
// range {% ... %} блока покрывает только содержимое между разделителями; каждый
// {% ... %} — отдельный EmbedExpr-узел со своим range.
TEST(TranspileLspTest, EmbedBlockRangeCoversOnlyBody) {
    Context ctx(".");
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
    const std::string code = "{% int x = 42; %} {% printf(\"hi\"); %}";
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
// узла совпадает со строкой начала следующего — между ними '\n' не вставляется.
TEST(TranspileLspTest, SameLineStatementsJoinOnOneLine) {
    Context ctx(".");
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
    EXPECT_NE(cpp.find("std::any a = 1; std::any b = 2;"), std::string::npos) << cpp;
    EXPECT_EQ(cpp.find("std::any a = 1;\nstd::any b = 2;"), std::string::npos) << cpp;
}

// ============================================================================
// 6b3. Function body mirrors the source layout ({ / } / statements on same lines)
// ============================================================================
// Сгенерированный C++ повторяет раскладку исходника и внутри блоков: операторы на
// одной строке исходника — на одной строке, '{'/'}' следуют строкам исходника.
TEST(TranspileLspTest, FunctionBodyProperFormatting) {
    // Одно-строчный блок: нормальное многострочное форматирование с отступами.
    {
        Context ctx(".");
        const std::string code = "%foo():Void ::= { a := 1; b := 2; }\n";
        MapperFile trustIdx = ctx.source().add_source("test.src", code);
        MapperFile cppIdx = ctx.source().add_output("test.cppt");
        trust::PipelineOpts popts;
        popts.input_file = "test.src";
        trust::Pipeline pipeline(ctx, popts);
        pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
        ASSERT_EQ(ctx.diag().errorCount(), 0);
        std::string cpp(ctx.source().output_body(cppIdx));
        // Операторы тела функции — с отступом, каждый на своей строке.
        EXPECT_NE(cpp.find("void foo() {\n    std::any a = 1;\n    std::any b = 2;\n}"), std::string::npos) << cpp;
    }
    // Много-строчный блок: то же нормальное форматирование с отступами.
    {
        Context ctx(".");
        const std::string code = "%foo():Void ::= {\n  a := 1;\n  b := 2;\n}\n";
        MapperFile trustIdx = ctx.source().add_source("test.src", code);
        MapperFile cppIdx = ctx.source().add_output("test.cppt");
        trust::PipelineOpts popts;
        popts.input_file = "test.src";
        trust::Pipeline pipeline(ctx, popts);
        pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
        ASSERT_EQ(ctx.diag().errorCount(), 0);
        std::string cpp(ctx.source().output_body(cppIdx));
        EXPECT_NE(cpp.find("void foo() {\n    std::any a = 1;\n    std::any b = 2;\n}"), std::string::npos) << cpp;
    }
}

// ============================================================================
// 6c. Multiple EMBED blocks on DIFFERENT lines: each on its own C++ line
// ============================================================================
// Блоки на разных строках исходника не конкатенируются: каждый попадает в C++ на
// отдельной строке и имеет собственный source-map range.
TEST(TranspileLspTest, MultipleEmbedBlocksSeparateLines) {
    Context ctx(".");
    const std::string code = "{% int x = 42; %}\n{% printf(\"hi\"); %}\n";
    MapperFile trustIdx = ctx.source().add_source("test.src", code);
    MapperFile cppIdx = ctx.source().add_output("test.cppt");

    trust::PipelineOpts popts;
    popts.input_file = "test.src";
    trust::Pipeline pipeline(ctx, popts);
    pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    std::string cpp(ctx.source().output_body(cppIdx));
    // Каждый блок — на отдельной строке.
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
        if (t == " int x = 42; ")
            foundFirst = true;
        if (t == " printf(\"hi\"); ")
            foundSecond = true;
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
// Диапазон statement'а функции — [имя, оператор] (НЕ тело): он маппится на сигнатуру
// C++ ("void func()"). Тело (блок m_right) маппится отдельно, чтобы скобки { } были
// видны в C++, а имя функции — отдельным name-маппингом на "func".
TEST(TranspileLspTest, FuncDeclSignatureAndBodyMappedSeparately) {
    Context ctx(".");
    const std::string code = "%func():None ::= { x := 1;\n}\n";
    MapperFile trustIdx = ctx.source().add_source("test.src", code);
    MapperFile cppIdx = ctx.source().add_output("test.cppt");

    trust::PipelineOpts popts;
    popts.input_file = "test.src";
    trust::Pipeline pipeline(ctx, popts);
    pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trustIdx, cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    std::string cpp(ctx.source().output_body(cppIdx));
    EXPECT_NE(cpp.find("void func()"), std::string::npos) << cpp;

    auto* reader = ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    uint32_t uIdx = 0;
    ASSERT_TRUE(findInputBySource(reader, code, uIdx));
    ReaderFile rTrust = ReaderFile::make_input(uIdx);

    // 1) Сигнатура: from = [имя, оператор] ("func():None ::="; диапазон имени m_left
    //    начинается с 'func', без '%'-префикса), to = "void func()".
    //    Оператор-only "::=" маппинга быть не должно.
    bool foundSig = false;
    for (const auto& m : reader->getTrustFileMappings(rTrust)) {
        std::string trustText(reader->getText(m.from));
        EXPECT_NE(trustText, "::=") << "operator-only mapping must not survive";
        if (trustText == "func():None ::=") {
            foundSig = true;
            EXPECT_EQ(reader->getText(m.to), "void func()");
        }
    }
    EXPECT_TRUE(foundSig) << "signature mapping '[имя, оператор]' not found";

    // 2) Имя функции: name-маппинг trust "func" -> cpp "func".
    bool foundName = false;
    for (const auto& nm : reader->getNameMappings()) {
        if (std::string(reader->getText(nm.rangeMap.from)) == "func") {
            foundName = true;
            EXPECT_EQ(reader->getText(nm.rangeMap.to), "func");
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
