#include "formatter/format_test_util.hpp"

namespace trust::formatter {
TEST(FormatterTest, FunctionBodyIndent) {
    const std::string in = "@main() := {\n@print('hi');\n}";
    const std::string exp = "@main() := {\n    @print('hi');\n}\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, SpaceNormalizationBinary) {
    const std::string in = "a:=1+2*3;";
    const std::string exp = "a := 1 + 2 * 3;\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, DictAndCall) {
    const std::string in = "d:=(count=7,pi=3.14,);\n@print('count={}\\n',d.count);";
    const std::string exp = "d := (count = 7, pi = 3.14,);\n@print('count={}\\n', d.count);\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, NestedBlocks) {
    const std::string in = "@main() := { if (x) { @print(1); }; @print(2); }";
    const std::string exp = "@main() := {\n"
                            "    if (x) {\n"
                            "        @print(1);\n"
                            "    };\n"
                            "    @print(2);\n"
                            "}\n";
    EXPECT_EQ(fmt(in), exp);
}
TEST(FormatterTest, VerbatimEmbed) {
    const std::string in = "x := {% int y = 1; %};\n";
    // {% ... %} — один токен EMBED; сохраняется как есть, окружение нормализуется.
    const std::string exp = "x := {% int y = 1; %};\n";
    EXPECT_EQ(fmt(in), exp);
}
TEST(FormatterTest, StringAndCharLiterals) {
    const std::string in = "s:=\"hello world\";\nc:='x';";
    const std::string exp = "s := \"hello world\";\nc := 'x';\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, NegativeLiteralAfterOperandIsSubtraction) {
    // `n-1` в токенах — это `n`, `-1`; грамматика трактует как `n - 1`.
    const std::string in = "x := fib(n-1);";
    const std::string exp = "x := fib(n - 1);\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, NegativeLiteralInUnaryPositionPreserved) {
    // `-5` после `:=` — отрицательный литерал, не расщепляется.
    const std::string in = "x := -5;";
    const std::string exp = "x := -5;\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, NegativeLiteralAfterOperatorPreserved) {
    // `a * -1` — умножение на отрицательный литерал.
    const std::string in = "y := a * -1;";
    const std::string exp = "y := a * -1;\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, SpacedMinusStaysBinary) {
    const std::string in = "y := a - 5;";
    const std::string exp = "y := a - 5;\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, NegativeLiteralAfterCloseParen) {
    const std::string in = "y := f()-1;";
    const std::string exp = "y := f() - 1;\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, SemicolonAfterBraceStaysSameLine) {
    const std::string in = "@main() := {\n    @print(1);\n};";
    const std::string exp = "@main() := {\n    @print(1);\n};\n";
    EXPECT_EQ(fmt(in), exp);
    // Идемпотентность: повторное форматирование не меняет результат.
    EXPECT_EQ(fmt(fmt(in)), exp);
}
TEST(FormatterTest, NoParenKeywordAlwaysSpaced) {
    const std::string in = "@main() := {\n"
                           "    @return :Tuple(1\\1, 1\\1);\n"
                           "    @return (p.0 + p.1, p.sum,):Tuple;\n"
                           "    return :Tuple(1\\1, 1\\1);\n"
                           "    @return x;\n"
                           "}\n";
    const std::string exp = "@main() := {\n"
                            "    @return :Tuple(1\\1, 1\\1);\n"
                            "    @return (p.0 + p.1, p.sum,):Tuple;\n"
                            "    return :Tuple(1\\1, 1\\1);\n"
                            "    @return x;\n"
                            "}\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

} // namespace trust::formatter
