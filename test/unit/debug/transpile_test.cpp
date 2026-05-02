#include "transpiler.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "trust_source.h"

// ============================================================================
// Helper: join vector to string for diagnostics
// ============================================================================
static std::string join(const std::vector<std::string> &lines) {
    std::string result;
    for (const auto &l : lines) {
        result += l;
        result += '\n';
    }
    return result;
}

// ============================================================================
// 1. Create statement
// ============================================================================
TEST(TranspileTest, CreateSimple) {
    std::vector<std::string> input = {"create x = 42;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 5) << join(output);
    EXPECT_EQ(output[3], "int cpp_x = 42;");
    EXPECT_EQ(output[output.size() - 2], "return 0;");
    EXPECT_EQ(output[output.size() - 1], "}");
}

TEST(TranspileTest, CreateFromOtherVar) {
    std::vector<std::string> input = {"create a = 100;", "create b = a;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 6) << join(output);
    EXPECT_EQ(output[3], "int cpp_a = 100;");
    EXPECT_EQ(output[4], "int cpp_b = cpp_a;");
}

TEST(TranspileTest, CreateWithExpression) {
    std::vector<std::string> input = {"create x = 5;", "create y = 10;", "create z = x + y;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 7) << join(output);
    EXPECT_EQ(output[3], "int cpp_x = 5;");
    EXPECT_EQ(output[4], "int cpp_y = 10;");
    EXPECT_EQ(output[5], "int cpp_z = cpp_x + cpp_y;");
}

// ============================================================================
// 2. Assignment
// ============================================================================
TEST(TranspileTest, AssignSimple) {
    std::vector<std::string> input = {"create x = 42;", "x = 10;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 6) << join(output);
    EXPECT_EQ(output[3], "int cpp_x = 42;");
    EXPECT_EQ(output[4], "cpp_x = 10;");
}

TEST(TranspileTest, AssignExpression) {
    std::vector<std::string> input = {"create a = 1;", "create b = 2;", "a = a + b;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 7) << join(output);
    EXPECT_EQ(output[5], "cpp_a = cpp_a + cpp_b;");
}

// ============================================================================
// 3. Print statement
// ============================================================================
TEST(TranspileTest, PrintSingleVar) {
    std::vector<std::string> input = {"create x = 42;", "print x;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 6) << join(output);
    EXPECT_EQ(output[4], "std::cout << cpp_x;");
}

TEST(TranspileTest, PrintMultipleArgs) {
    std::vector<std::string> input = {"create a = 10;", "create b = 20;", "print a b;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 7) << join(output);
    EXPECT_EQ(output[5], "std::cout << cpp_a << cpp_b;");
}

TEST(TranspileTest, PrintLiteralNumber) {
    std::vector<std::string> input = {"print 42;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 5) << join(output);
    // 42 — число, не объявленная переменная — выводится как есть
    EXPECT_EQ(output[3], "std::cout << 42;");
}

// ============================================================================
// 4. Comments (lines starting with '#')
// ============================================================================
TEST(TranspileTest, SkipComments) {
    std::vector<std::string> input = {"# this is a comment", "", "create x = 5;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 5) << join(output);
    EXPECT_EQ(output[3], "int cpp_x = 5;");
    // Проверяем, что не было лишних строк
    // 3 строки шапки + 1 create + return + }
    EXPECT_EQ(output.size(), 6) << join(output);
}

// ============================================================================
// 5. Full example (from the spec)
// ============================================================================
TEST(TranspileTest, FullExample) {
    std::vector<std::string> input = {"# my program", "create a = 10;", "create x = 10;", "create b = 20;", "a = a + b;", "print a b;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 9) << join(output);

    EXPECT_EQ(output[0], "#include <iostream>");
    EXPECT_EQ(output[1], "");
    EXPECT_EQ(output[2], "int main() {");

    EXPECT_EQ(output[3], "int cpp_a = 10;");
    EXPECT_EQ(output[4], "int cpp_x = 10;");
    EXPECT_EQ(output[5], "int cpp_b = 20;");
    EXPECT_EQ(output[6], "cpp_a = cpp_a + cpp_b;");
    EXPECT_EQ(output[7], "std::cout << cpp_a << cpp_b;");

    EXPECT_EQ(output[8], "return 0;");
    EXPECT_EQ(output[9], "}");
}

// ============================================================================
// 6. Multiple operators in expression
// ============================================================================
TEST(TranspileTest, ComplexExpression) {
    std::vector<std::string> input = {"create x = 10;", "create y = 20;", "create z = 30;", "x = x + y - z;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 8) << join(output);
    EXPECT_EQ(output[6], "cpp_x = cpp_x + cpp_y - cpp_z;");
}

// ============================================================================
// 7. No trailing semicolon (should still work)
// ============================================================================
TEST(TranspileTest, NoSemicolon) {
    std::vector<std::string> input = {"create x = 42"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 5) << join(output);
    EXPECT_EQ(output[3], "int cpp_x = 42;");
}

// ============================================================================
// 8. Undeclared variable in assignment — should emit error comment
// ============================================================================
TEST(TranspileTest, UndeclaredVarAssignment) {
    std::vector<std::string> input = {"x = 42;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 5) << join(output);
    EXPECT_TRUE(output[3].find("ERROR:") != std::string::npos);
}

// ============================================================================
// 9. Header structure
// ============================================================================
TEST(TranspileTest, HeaderStructure) {
    std::vector<std::string> input = {"create x = 1;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 3);
    EXPECT_EQ(output[0], "#include <iostream>");
    EXPECT_EQ(output[1], "");
    EXPECT_EQ(output[2], "int main() {");

    ASSERT_EQ(1, _m.entries().size());
    auto &ent = _m.entries();
    EXPECT_EQ(3, ent[0].cpp_line_inserted);
}

// ============================================================================
// 10. Multiple create statements on one line
// ============================================================================
TEST(TranspileTest, TwoCreateSameLine) {
    std::vector<std::string> input = {"create x = 5; create y = 10;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 5) << join(output);
    EXPECT_EQ(output[3], "int cpp_x = 5; int cpp_y = 10;");
    EXPECT_EQ(output.size(), 6) << join(output);
}

TEST(TranspileTest, ThreeCreateSameLine) {
    std::vector<std::string> input = {"create x = 1; create y = 2; create z = 3;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 5) << join(output);
    EXPECT_EQ(output[3], "int cpp_x = 1; int cpp_y = 2; int cpp_z = 3;");
    EXPECT_EQ(output.size(), 6) << join(output);
}

// ============================================================================
// 11. Create and print on same line
// ============================================================================
TEST(TranspileTest, CreateAndPrintSameLine) {
    std::vector<std::string> input = {"create x = 42; print x;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 5) << join(output);
    EXPECT_EQ(output[3], "int cpp_x = 42; std::cout << cpp_x;");
}

// ============================================================================
// 12. Multiple statements with blank/comment lines between them
// ============================================================================
TEST(TranspileTest, StatementsWithBlankLines) {
    std::vector<std::string> input = {
        "# header",
        "",
        "create x = 10;",
        "",
        "# middle",
        "create y = 20; print y;",
        "",
        "x = x + y;"
    };
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    auto output = transpile(input, _m);

    ASSERT_GE(output.size(), 7) << join(output);
    EXPECT_EQ(output[3], "int cpp_x = 10;");
    EXPECT_EQ(output[4], "int cpp_y = 20; std::cout << cpp_y;");
    EXPECT_EQ(output[5], "cpp_x = cpp_x + cpp_y;");
}

// ============================================================================
// 13. Mapping tests: multiple variables on one line
// ============================================================================
TEST(TranspileTest, GetCppVarTwoVarsOnSameLine) {
    std::vector<std::string> input = {"create a = 10; create b = 20;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    transpile(input, _m);

    // Both variables should be findable via getCppVar
    auto info_a = _m.getCppVar("test.src", 1, "a");
    ASSERT_TRUE(info_a.has_value());
    EXPECT_EQ(info_a->vars.first, "a");
    EXPECT_EQ(info_a->vars.second, "cpp_a");
    EXPECT_EQ(info_a->lines.first, 1);   // trust line
    EXPECT_EQ(info_a->lines.second, 1);  // cpp line (original, without inserted)

    auto info_b = _m.getCppVar("test.src", 1, "b");
    ASSERT_TRUE(info_b.has_value());
    EXPECT_EQ(info_b->vars.first, "b");
    EXPECT_EQ(info_b->vars.second, "cpp_b");
    EXPECT_EQ(info_b->lines.first, 1);   // trust line
    EXPECT_EQ(info_b->lines.second, 1);  // cpp line (original, without inserted)
}

TEST(TranspileTest, GetTrustVarTwoVarsOnSameLine) {
    std::vector<std::string> input = {"create a = 10; create b = 20;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    transpile(input, _m);

    auto info_a = _m.getTrustVar("test.cpp", 1, "cpp_a");
    ASSERT_TRUE(info_a.has_value());
    EXPECT_EQ(info_a->vars.first, "a");
    EXPECT_EQ(info_a->vars.second, "cpp_a");

    auto info_b = _m.getTrustVar("test.cpp", 1, "cpp_b");
    ASSERT_TRUE(info_b.has_value());
    EXPECT_EQ(info_b->vars.first, "b");
    EXPECT_EQ(info_b->vars.second, "cpp_b");
}

// ============================================================================
// 14. Nearest mapping tests with empty lines
// ============================================================================
TEST(NearestMappingTest, TrustToCppWithEmptyLines) {
    std::vector<std::string> input = {
        "# comment",
        "",
        "create x = 5;",
        "",
        "# another comment",
        "print x;"
    };
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    transpile(input, _m);

    // trust line 3 (create x) should map to cpp line (1 + cpp_line_inserted)
    auto info_create = _m.nearestTrustToCpp("test.src", 3);
    ASSERT_TRUE(info_create.has_value());
    EXPECT_EQ(info_create->second, 4); // 1 + 3 (inserted preamble)

    // trust line 6 (print x) should map to cpp line (2 + cpp_line_inserted)
    auto info_print = _m.nearestTrustToCpp("test.src", 6);
    ASSERT_TRUE(info_print.has_value());
    EXPECT_EQ(info_print->second, 5); // 2 + 3 (inserted preamble)
}

TEST(TranspileTest, NearestCppToTrustWithEmptyLines) {
    std::vector<std::string> input = {
        "create x = 5;",
        "",
        "print x;"
    };
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    transpile(input, _m);

    // cpp line 1 → trust line 1 (create) — raw index has {1→1}
    auto info_cpp1 = _m.nearestCppToTrust("test.cpp", 1);
    ASSERT_TRUE(info_cpp1.has_value());
    EXPECT_EQ(info_cpp1->second, 1);

    // cpp line 2 → trust line 3 (print) — raw index has {3→2}
    auto info_cpp2 = _m.nearestCppToTrust("test.cpp", 2);
    ASSERT_TRUE(info_cpp2.has_value());
    EXPECT_EQ(info_cpp2->second, 3);
}

// ============================================================================
// 15. Nearest mapping with multiple variables on same line
// ============================================================================
TEST(TranspileTest, NearestMappingMultiVarSameLine) {
    std::vector<std::string> input = {"create a = 1; create b = 2; create c = 3;"};
    trust::TrustSource _m;
    _m.setFilePair("test.src", "test.cpp");
    transpile(input, _m);

    // trust line 1 → cpp line 4 (1 + 3 inserted preamble)
    auto info = _m.nearestTrustToCpp("test.src", 1);
    ASSERT_TRUE(info.has_value());
    // Теперь nearestTrustToCpp корректно учитывает cpp_line_inserted
    EXPECT_EQ(info->second, 4);

    // In reverse: cpp line 4 → trust line 1
    auto rev4 = _m.nearestCppToTrust("test.cpp", 4);
    ASSERT_TRUE(rev4.has_value());
    EXPECT_EQ(rev4->second, 1);
}