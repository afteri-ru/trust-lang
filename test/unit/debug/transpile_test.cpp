#include "lsp/transpile.h"

#include "diag/context.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace trust;

// Helper: получить reader после транспиляции
static const SourceMapReader* getReader(Context& ctx) {
    const auto* r = ctx.toReader();
    EXPECT_NE(r, nullptr);
    return r;
}

// Helper: проверить маппинг по индексу
static void checkMapping(const SourceMapReader* reader, ReaderFile rFileIdx, size_t idx, std::string_view expectedTrustText, std::string_view expectedCppText) {
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_GT(mappings.size(), idx);
    EXPECT_EQ(reader->getText(mappings[idx].from), expectedTrustText);
    EXPECT_EQ(reader->getText(mappings[idx].to), expectedCppText);
}

// ============================================================================
// 1. Create statement
// ============================================================================
TEST(TranspileTest, CreateSimple) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 42;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 1);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 42");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 42");

    // roundtrip: trust → cpp
    auto roundtrip = reader->getMapTrustToCpp(mappings[0].from.begin);
    ASSERT_TRUE(roundtrip.has_value());
    EXPECT_EQ(roundtrip->begin, mappings[0].to.begin);
    EXPECT_EQ(roundtrip->end, mappings[0].to.end);

    // cpp → trust
    auto roundtripBack = reader->getMapCppToTrust(mappings[0].to.begin);
    ASSERT_TRUE(roundtripBack.has_value());
    EXPECT_EQ(roundtripBack->begin, mappings[0].from.begin);

    // проверка структуры output
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_NE(cpp.find("int cpp_x = 42;"), std::string::npos);
    EXPECT_NE(cpp.find("return 0;"), std::string::npos);
}

TEST(TranspileTest, CreateFromOtherVar) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create a = 100;\ncreate b = a;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 2);

    EXPECT_EQ(reader->getText(mappings[0].from), "create a = 100");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_a = 100");

    EXPECT_EQ(reader->getText(mappings[1].from), "create b = a");
    EXPECT_EQ(reader->getText(mappings[1].to), "int cpp_b = cpp_a");
}

TEST(TranspileTest, CreateWithExpression) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 5;\ncreate y = 10;\ncreate z = x + y;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 3);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 5");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 5");

    EXPECT_EQ(reader->getText(mappings[1].from), "create y = 10");
    EXPECT_EQ(reader->getText(mappings[1].to), "int cpp_y = 10");

    EXPECT_EQ(reader->getText(mappings[2].from), "create z = x + y");
    EXPECT_EQ(reader->getText(mappings[2].to), "int cpp_z = cpp_x + cpp_y");
}

// ============================================================================
// 2. Assignment
// ============================================================================
TEST(TranspileTest, AssignSimple) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 42;\nx = 10;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 2);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 42");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 42");

    EXPECT_EQ(reader->getText(mappings[1].from), "x = 10");
    EXPECT_EQ(reader->getText(mappings[1].to), "cpp_x = 10");
}

TEST(TranspileTest, AssignExpression) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create a = 1;\ncreate b = 2;\na = a + b;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 3);

    EXPECT_EQ(reader->getText(mappings[0].from), "create a = 1");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_a = 1");

    EXPECT_EQ(reader->getText(mappings[1].from), "create b = 2");
    EXPECT_EQ(reader->getText(mappings[1].to), "int cpp_b = 2");

    EXPECT_EQ(reader->getText(mappings[2].from), "a = a + b");
    EXPECT_EQ(reader->getText(mappings[2].to), "cpp_a = cpp_a + cpp_b");
}

// ============================================================================
// 3. Print statement
// ============================================================================
TEST(TranspileTest, PrintSingleVar) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 42;\nprint x;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 2);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 42");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 42");

    EXPECT_EQ(reader->getText(mappings[1].from), "print x");
    EXPECT_EQ(reader->getText(mappings[1].to), "std::cout << cpp_x");
}

TEST(TranspileTest, PrintMultipleArgs) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create a = 10;\ncreate b = 20;\nprint a b;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 3);

    EXPECT_EQ(reader->getText(mappings[0].from), "create a = 10");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_a = 10");

    EXPECT_EQ(reader->getText(mappings[1].from), "create b = 20");
    EXPECT_EQ(reader->getText(mappings[1].to), "int cpp_b = 20");

    EXPECT_EQ(reader->getText(mappings[2].from), "print a b");
    EXPECT_EQ(reader->getText(mappings[2].to), "std::cout << cpp_a << cpp_b");
}

TEST(TranspileTest, PrintLiteralNumber) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("print 42;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 1);

    EXPECT_EQ(reader->getText(mappings[0].from), "print 42");
    EXPECT_EQ(reader->getText(mappings[0].to), "std::cout << 42");
}

// ============================================================================
// 4. Comments (lines starting with '#')
// ============================================================================
TEST(TranspileTest, SkipComments) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("# this is a comment\n\ncreate x = 5;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 1);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 5");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 5");

    // Всего 6 строк: 2 preamble + 4 body (create, return, closing brace, final newline)
    std::string cpp(ctx.output_body(cppIdx));
    auto lines_count = [](const std::string& s) { return std::count(s.begin(), s.end(), '\n'); };
    EXPECT_EQ(lines_count(cpp), 6);
}

// ============================================================================
// 5. Full example
// ============================================================================
TEST(TranspileTest, FullExample) {
    Context ctx(".");
    auto [trustIdx, cppIdx] =
        lsp::transpile("# my program\ncreate a = 10;\ncreate x = 10;\ncreate b = 20;\na = a + b;\nprint a b;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 5);

    EXPECT_EQ(reader->getText(mappings[0].from), "create a = 10");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_a = 10");

    EXPECT_EQ(reader->getText(mappings[1].from), "create x = 10");
    EXPECT_EQ(reader->getText(mappings[1].to), "int cpp_x = 10");

    EXPECT_EQ(reader->getText(mappings[2].from), "create b = 20");
    EXPECT_EQ(reader->getText(mappings[2].to), "int cpp_b = 20");

    EXPECT_EQ(reader->getText(mappings[3].from), "a = a + b");
    EXPECT_EQ(reader->getText(mappings[3].to), "cpp_a = cpp_a + cpp_b");

    EXPECT_EQ(reader->getText(mappings[4].from), "print a b");
    EXPECT_EQ(reader->getText(mappings[4].to), "std::cout << cpp_a << cpp_b");

    // Проверяем структуру output
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_NE(cpp.find("#include <iostream>"), std::string::npos);
    EXPECT_NE(cpp.find("int main() {"), std::string::npos);
    EXPECT_NE(cpp.find("return 0;"), std::string::npos);
}

// ============================================================================
// 6. Multiple operators in expression
// ============================================================================
TEST(TranspileTest, ComplexExpression) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 10;\ncreate y = 20;\ncreate z = 30;\nx = x + y - z;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 4);

    EXPECT_EQ(reader->getText(mappings[3].from), "x = x + y - z");
    EXPECT_EQ(reader->getText(mappings[3].to), "cpp_x = cpp_x + cpp_y - cpp_z");
}

// ============================================================================
// 7. No trailing semicolon
// ============================================================================
TEST(TranspileTest, NoSemicolon) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 42", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 1);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 42");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 42");
}

// ============================================================================
// 8. Undeclared variable in assignment — error via diag
// ============================================================================
TEST(TranspileTest, UndeclaredVarAssignment) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("x = 42;\n", "test.src", "test.cpp", ctx);

    // Ошибка сообщается через diag, а не через expected
    EXPECT_GT(ctx.diag().errorCount(), 0);
}

// ============================================================================
// 9. Header structure + mapping
// ============================================================================
TEST(TranspileTest, HeaderStructure) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 1;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_NE(cpp.find("#include <iostream>"), std::string::npos);
    EXPECT_NE(cpp.find("int main() {"), std::string::npos);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_GE(mappings.size(), 1);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 1");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 1");
}

// ============================================================================
// 10. Multiple create statements on one line
// ============================================================================
TEST(TranspileTest, TwoCreateSameLine) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 5; create y = 10;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 2);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 5");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 5");

    EXPECT_EQ(reader->getText(mappings[1].from), "create y = 10");
    EXPECT_EQ(reader->getText(mappings[1].to), "int cpp_y = 10");

    EXPECT_EQ(reader->line(mappings[0].to.begin), 4);
    EXPECT_EQ(reader->line(mappings[0].to.begin), reader->line(mappings[1].to.begin));
}

TEST(TranspileTest, ThreeCreateSameLine) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 1; create y = 2; create z = 3;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 3);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 1");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 1");

    EXPECT_EQ(reader->getText(mappings[1].from), "create y = 2");
    EXPECT_EQ(reader->getText(mappings[1].to), "int cpp_y = 2");

    EXPECT_EQ(reader->getText(mappings[2].from), "create z = 3");
    EXPECT_EQ(reader->getText(mappings[2].to), "int cpp_z = 3");

    EXPECT_EQ(reader->line(mappings[0].to.begin), 4);
    EXPECT_EQ(reader->line(mappings[0].to.begin), reader->line(mappings[1].to.begin));
    EXPECT_EQ(reader->line(mappings[0].to.begin), reader->line(mappings[2].to.begin));
}

// ============================================================================
// 11. Create and print on same line
// ============================================================================
TEST(TranspileTest, CreateAndPrintSameLine) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 42; print x;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 2);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 42");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 42");

    EXPECT_EQ(reader->getText(mappings[1].from), "print x");
    EXPECT_EQ(reader->getText(mappings[1].to), "std::cout << cpp_x");

    EXPECT_EQ(reader->line(mappings[0].to.begin), reader->line(mappings[1].to.begin));
}

// ============================================================================
// 12. Multiple statements with blank/comment lines
// ============================================================================
TEST(TranspileTest, StatementsWithBlankLines) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("# header\n\ncreate x = 10;\n\n# middle\ncreate y = 20; print y;\n\nx = x + y;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 4);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 10");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 10");

    EXPECT_EQ(reader->getText(mappings[1].from), "create y = 20");
    EXPECT_EQ(reader->getText(mappings[1].to), "int cpp_y = 20");

    EXPECT_EQ(reader->getText(mappings[2].from), "print y");
    EXPECT_EQ(reader->getText(mappings[2].to), "std::cout << cpp_y");

    EXPECT_EQ(reader->getText(mappings[3].from), "x = x + y");
    EXPECT_EQ(reader->getText(mappings[3].to), "cpp_x = cpp_x + cpp_y");
}

// ============================================================================
// 13. Mapping entry count and content verification
// ============================================================================
TEST(TranspileTest, MappingEntryCount) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create a = 1; create b = 2; create c = 3;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 3);

    // Проверяем, что все начала уникальны (разные Location)
    EXPECT_NE(mappings[0].from.begin, mappings[1].from.begin);
    EXPECT_NE(mappings[1].from.begin, mappings[2].from.begin);
    EXPECT_NE(mappings[0].from.end, mappings[1].from.end);

    EXPECT_NE(mappings[0].to.begin, mappings[1].to.begin);
    EXPECT_NE(mappings[1].to.begin, mappings[2].to.begin);
    EXPECT_NE(mappings[0].to.end, mappings[1].to.end);

    // Порядок сортировки: from.begin по возрастанию в пределах файла
    EXPECT_LT(mappings[0].from.begin, mappings[1].from.begin);
    EXPECT_LT(mappings[1].from.begin, mappings[2].from.begin);

    EXPECT_LT(mappings[0].to.begin, mappings[1].to.begin);
    EXPECT_LT(mappings[1].to.begin, mappings[2].to.begin);
}

// ============================================================================
// 14. Nearest mapping tests with empty lines
// ============================================================================
TEST(TranspileTest, TrustToCppWithEmptyLines) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("# comment\n\ncreate x = 5;\n\n# another comment\nprint x;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 2);

    // first mapping (create x = 5) on trust line 3 → cpp line 4
    EXPECT_EQ(reader->line(mappings[0].from.begin), 3);
    EXPECT_EQ(reader->line(mappings[0].to.begin), 4);
    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 5");

    // second mapping (print x) on trust line 6 → cpp line 5
    EXPECT_EQ(reader->line(mappings[1].from.begin), 6);
    EXPECT_EQ(reader->line(mappings[1].to.begin), 5);
    EXPECT_EQ(reader->getText(mappings[1].from), "print x");
}

TEST(TranspileTest, ReverseCppToTrustWithEmptyLines) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 5;\n\nprint x;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 2);

    // first mapping (create x = 5) on trust line 1 → cpp line 4
    EXPECT_EQ(reader->line(mappings[0].from.begin), 1);
    EXPECT_EQ(reader->line(mappings[0].to.begin), 4);
    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 5");

    // second mapping (print x) on trust line 3 → cpp line 5
    EXPECT_EQ(reader->line(mappings[1].from.begin), 3);
    EXPECT_EQ(reader->line(mappings[1].to.begin), 5);
    EXPECT_EQ(reader->getText(mappings[1].from), "print x");
}

// ============================================================================
// 15. Nearest mapping with multiple variables on same line
// ============================================================================
TEST(TranspileTest, NearestMappingMultiVarSameLine) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create a = 1; create b = 2; create c = 3;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 3);

    // all three are on trust line 1, cpp line 4
    EXPECT_EQ(reader->line(mappings[0].from.begin), 1);
    EXPECT_EQ(reader->line(mappings[0].to.begin), 4);

    EXPECT_EQ(reader->line(mappings[1].from.begin), 1);
    EXPECT_EQ(reader->line(mappings[1].to.begin), 4);

    EXPECT_EQ(reader->line(mappings[2].from.begin), 1);
    EXPECT_EQ(reader->line(mappings[2].to.begin), 4);
}

// ============================================================================
// 16. Mapping with gaps (comments, blank lines)
// ============================================================================
TEST(TranspileTest, MappingWithGaps) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("# start\n\ncreate x = 10;\n# middle\n\nprint x;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 2);

    EXPECT_EQ(reader->getText(mappings[0].from), "create x = 10");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 10");

    EXPECT_EQ(reader->getText(mappings[1].from), "print x");
    EXPECT_EQ(reader->getText(mappings[1].to), "std::cout << cpp_x");
}

// ============================================================================
// 17. Macro definition and usage
// ============================================================================
TEST(TranspileTest, MacroDefineAndUse) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("macro MyMacro create x = 42;\nMyMacro;\n", "test.src", "test.cpp", ctx);

    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_NE(cpp.find("int cpp_x = 42;"), std::string::npos);
    EXPECT_NE(cpp.find("return 0;"), std::string::npos);
}

TEST(TranspileTest, MacroUseTwice) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("macro Init create x = 10;\nInit;\ncreate y = 5;\nInit;\n", "test.src", "test.cpp", ctx);
    auto* reader = getReader(ctx);

    auto rFileIdx = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(rFileIdx);
    ASSERT_EQ(mappings.size(), 3);

    // first macro expansion
    EXPECT_EQ(reader->getText(mappings[0].from), "Init");
    EXPECT_EQ(reader->getText(mappings[0].to), "int cpp_x = 10");

    // create y = 5
    EXPECT_EQ(reader->getText(mappings[1].from), "create y = 5");
    EXPECT_EQ(reader->getText(mappings[1].to), "int cpp_y = 5");

    // second macro expansion
    EXPECT_EQ(reader->getText(mappings[2].from), "Init");
    EXPECT_EQ(reader->getText(mappings[2].to), "int cpp_x = 10");

    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_NE(cpp.find("int cpp_x = 10;"), std::string::npos);
    EXPECT_NE(cpp.find("int cpp_y = 5;"), std::string::npos);
    EXPECT_NE(cpp.find("return 0;"), std::string::npos);
}

// ============================================================================
// 18. if/then with literal condition
// ============================================================================
TEST(TranspileTest, IfThenLiteral) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("if 1 then print \"yes\";\n", "test.src", "test.cpp", ctx);
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_TRUE(cpp.find("static_cast<bool>(1)") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << \"yes\"") != std::string::npos);
}

// ============================================================================
// 19. if/then/else with literal condition
// ============================================================================
TEST(TranspileTest, IfThenElseLiteral) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("if 0 then print \"yes\" else print \"no\";\n", "test.src", "test.cpp", ctx);
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_TRUE(cpp.find("static_cast<bool>(0)") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << \"yes\"") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << \"no\"") != std::string::npos);
}

// ============================================================================
// 20. if/then with variable condition
// ============================================================================
TEST(TranspileTest, IfThenVar) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 1;\nif x then print \"true\";\n", "test.src", "test.cpp", ctx);
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_TRUE(cpp.find("static_cast<bool>(cpp_x)") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << \"true\"") != std::string::npos);
}

// ============================================================================
// 21. if/then/else with variable condition
// ============================================================================
TEST(TranspileTest, IfThenElseVar) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 0;\nif x then print \"yes\" else print \"no\";\n", "test.src", "test.cpp", ctx);
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_TRUE(cpp.find("static_cast<bool>(cpp_x)") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << \"yes\"") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << \"no\"") != std::string::npos);
}

// ============================================================================
// 22. While loop with literal condition
// ============================================================================
TEST(TranspileTest, WhileLiteral) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("while 1 { print \"loop\"; }\n", "test.src", "test.cpp", ctx);
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_TRUE(cpp.find("static_cast<bool>(1)") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << \"loop\"") != std::string::npos);
    EXPECT_TRUE(cpp.find("return 0;") != std::string::npos);
}

// ============================================================================
// 23. While loop with variable condition
// ============================================================================
TEST(TranspileTest, WhileVar) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 1;\nwhile x { print x; }\n", "test.src", "test.cpp", ctx);
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_TRUE(cpp.find("static_cast<bool>(cpp_x)") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << cpp_x") != std::string::npos);
    EXPECT_TRUE(cpp.find("return 0;") != std::string::npos);
}

// ============================================================================
// 24. While loop with complex body (assignment + print)
// ============================================================================
TEST(TranspileTest, WhileComplexBody) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 5;\nwhile x { print x; x = x - 1; }\n", "test.src", "test.cpp", ctx);
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_TRUE(cpp.find("static_cast<bool>(cpp_x)") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << cpp_x") != std::string::npos);
    EXPECT_TRUE(cpp.find("cpp_x = cpp_x - 1") != std::string::npos);
}

// ============================================================================
// 25. NameMapping verification for create
// ============================================================================
TEST(TranspileTest, NameMappingCreate) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 42;\n", "test.src", "test.cpp", ctx);
    auto* reader = ctx.toReader();
    ASSERT_NE(reader, nullptr);

    const auto& nameMappings = reader->getNameMappings();
    ASSERT_EQ(nameMappings.size(), 1);
    EXPECT_EQ(nameMappings[0].fromName, "x");
    EXPECT_EQ(nameMappings[0].toName, "cpp_x");

    // Проверяем, что NameMapping покрывает правильный диапазон
    auto trustFile = reader->findFileIdx("test.src");
    EXPECT_EQ(reader->getText(nameMappings[0].rangeMap.from), "create x = 42");
    EXPECT_EQ(reader->getText(nameMappings[0].rangeMap.to), "int cpp_x = 42");
}

// ============================================================================
// 26. NameMapping verification for assign
// ============================================================================
TEST(TranspileTest, NameMappingAssign) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("create x = 10;\nx = 20;\n", "test.src", "test.cpp", ctx);
    auto* reader = ctx.toReader();
    ASSERT_NE(reader, nullptr);

    const auto& nameMappings = reader->getNameMappings();
    ASSERT_EQ(nameMappings.size(), 2);

    // Первый NameMapping — для create
    EXPECT_EQ(nameMappings[0].fromName, "x");
    EXPECT_EQ(nameMappings[0].toName, "cpp_x");
    EXPECT_EQ(reader->getText(nameMappings[0].rangeMap.from), "create x = 10");
    EXPECT_EQ(reader->getText(nameMappings[0].rangeMap.to), "int cpp_x = 10");

    // Второй NameMapping — для assign
    EXPECT_EQ(nameMappings[1].fromName, "x");
    EXPECT_EQ(nameMappings[1].toName, "cpp_x");
    EXPECT_EQ(reader->getText(nameMappings[1].rangeMap.from), "x = 20");
    EXPECT_EQ(reader->getText(nameMappings[1].rangeMap.to), "cpp_x = 20");
}

// ============================================================================
// 27. addMacroMapping verification
// ============================================================================
TEST(TranspileTest, MacroMapping) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("macro MyMacro create x = 42;\nMyMacro;\n", "test.src", "test.cpp", ctx);
    auto* reader = ctx.toReader();
    ASSERT_NE(reader, nullptr);

    auto trustFile = reader->findFileIdx("test.src");
    auto mappings = reader->getTrustFileMappings(trustFile);
    ASSERT_GE(mappings.size(), 1);

    // Проверяем, что макрос-вызов MyMacro имеет addMacroMapping
    // Позиция вызова макроса — первый mapping (MyMacro; → int cpp_x = 42)
    auto macroDefRange = reader->getMacroDefRange(mappings[0].from.begin);
    ASSERT_TRUE(macroDefRange.has_value());
    // Тело макроса сохраняется включая точку с запятой (до конца строки)
    EXPECT_EQ(reader->getText(*macroDefRange), "create x = 42;");
}

// ============================================================================
// 28. Macro with composite body (while inside macro)
// ============================================================================
TEST(TranspileTest, MacroWithWhileBody) {
    Context ctx(".");
    auto [trustIdx, cppIdx] = lsp::transpile("macro Loop while 1 { print \"x\"; }\nLoop;\n", "test.src", "test.cpp", ctx);
    std::string cpp(ctx.output_body(cppIdx));
    EXPECT_TRUE(cpp.find("while (static_cast<bool>(1))") != std::string::npos);
    EXPECT_TRUE(cpp.find("std::cout << \"x\"") != std::string::npos);
}
