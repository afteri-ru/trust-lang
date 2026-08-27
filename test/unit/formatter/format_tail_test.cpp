#include "formatter/format_test_util.hpp"

namespace trust::formatter {
TEST(FormatterTest, TailPreserved_UnterminatedString) {
    const std::string in = "@main() := {\n    x := 1;\n    y := 'unterminated\n    z := 2;\n}\n";
    const std::string exp = "@main() := {\n    x := 1;\n    y := 'unterminated\n    z := 2;\n}\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

// Незакрытый блочный комментарий /* ... — лексер не зависает (правила <<EOF>> терминируют) и
// сохраняет текст комментария; хвост не теряется.
TEST(FormatterTest, TailPreserved_UnterminatedBlockComment) {
    const std::string in = "@main() := {\n    x := 1;  /* unterminated\n    y := 2;\n}\n";
    const std::string exp = "@main() := {\n    x := 1; /* unterminated\n    y := 2;\n}\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

// Неожиданный символ: лексер продолжает, последующие токены не теряются.
TEST(FormatterTest, TailPreserved_UnexpectedCharacter) {
    const std::string in = "@main() := {\n    x := 1;\n    y := 2;  \xc2\xa7"
                           "bad\n    z := 3;\n}\n";
    const std::string exp = "@main() := {\n    x := 1;\n    y := 2;\n    \xc2\xa7"
                            "bad z := 3;\n}\n";
    EXPECT_EQ(fmt(in), exp);
    EXPECT_EQ(fmt(fmt(in)), exp);
}

} // namespace trust::formatter
