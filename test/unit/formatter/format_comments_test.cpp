#include "formatter/format_test_util.hpp"

namespace trust::formatter {
TEST(FormatterTest, DocCommentOwnLine) {
    const std::string in = "## Док\n@main() := {}\n";
    // Пустой блок } сразу после { — остаётся на своей строке (не схлопываем в первой версии).
    const std::string exp = "## Док\n@main() := {\n}\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, LineCommentPreserved) {
    const std::string in = "a := 1; # comment\nb := 2;";
    const std::string exp = "a := 1;\n# comment\nb := 2;\n";
    EXPECT_EQ(fmt(in), exp);
}

TEST(FormatterTest, BlankLinesPreserved) {
    const std::string in = "a := 1;\n\n\nb := 2;";
    // Несколько пустых строк схлопываются в одну (идемпотентно).
    const std::string exp = "a := 1;\n\nb := 2;\n";
    EXPECT_EQ(fmt(in), exp);
}
TEST(FormatterTest, LeadingFileCommentPreserved) {
    const std::string in = "# header comment\n@main() := {}\n";
    const std::string exp = "# header comment\n@main() := {\n}\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, TrailingLineCommentAtEofPreserved) {
    const std::string in = "x := 1;\n# eof comment\n";
    const std::string exp = "x := 1;\n# eof comment\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, TrailingBlockCommentAtEofPreserved) {
    const std::string in = "x := 1;\n/* eof block */\n";
    const std::string exp = "x := 1;\n/* eof block */\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, InlineCommentAtEofPreserved) {
    const std::string in = "x := 1; # inline eof";
    const std::string exp = "x := 1;\n# inline eof\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, CommentOnlyFile) {
    const std::string in = "# just a comment\n";
    const std::string exp = "# just a comment\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, BlankLinesAroundCommentPreserved) {
    const std::string in = "a := 1;\n\n# comment\n\nb := 2;\n";
    const std::string exp = "a := 1;\n\n# comment\n\nb := 2;\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, BlankLinesBetweenCommentsPreserved) {
    const std::string in = "# c1\n\n# c2\nb := 1;\n";
    const std::string exp = "# c1\n\n# c2\nb := 1;\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

TEST(FormatterTest, BlockCommentOnOwnLineWithBlanksPreserved) {
    const std::string in = "a := 1;\n\n/* note */\n\nb := 2;\n";
    const std::string exp = "a := 1;\n\n/* note */\n\nb := 2;\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

} // namespace trust::formatter
