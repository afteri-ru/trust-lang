#include "formatter/format_test_util.hpp"

namespace trust::formatter {
TEST(FormatterTest, ConfigParseClassicKeys) {
    FormatConfig c = parseConfig("IndentWidth: 2\nUseTabs: true\nColumnLimit: 80\nInsertFinalNewline: false\n");
    ASSERT_TRUE(c.ok) << c.error;
    EXPECT_EQ(c.opts.tab_size, 2);
    EXPECT_FALSE(c.opts.use_spaces);
    EXPECT_EQ(c.opts.max_line_width, 80);
    EXPECT_FALSE(c.opts.insert_final_newline);
}

TEST(FormatterTest, ConfigParseKeywords) {
    FormatConfig c = parseConfig("Keywords: print,each,@foo\n");
    ASSERT_TRUE(c.ok) << c.error;
    ASSERT_EQ(c.opts.keywords.size(), 3u);
    EXPECT_EQ(c.opts.keywords[0], "print");
    EXPECT_EQ(c.opts.keywords[1], "each");
    EXPECT_EQ(c.opts.keywords[2], "@foo");
}

TEST(FormatterTest, ConfigParseUnknownKeyError) {
    FormatConfig c = parseConfig("BogusOption: 1\n");
    EXPECT_FALSE(c.ok);
    EXPECT_FALSE(c.error.empty());
}

TEST(FormatterTest, ConfigCommentsAndHeader) {
    FormatConfig c = parseConfig("---\n# comment\nIndentWidth: 3\n\n");
    ASSERT_TRUE(c.ok) << c.error;
    EXPECT_EQ(c.opts.tab_size, 3);
}

TEST(FormatterTest, DumpConfigRoundTrip) {
    const std::string dump = dumpConfig();
    // Dump содержит все ключи с дефолтами и комментариями.
    EXPECT_NE(dump.find("IndentWidth: 4"), std::string::npos);
    EXPECT_NE(dump.find("UseTabs: false"), std::string::npos);
    EXPECT_NE(dump.find("ColumnLimit: 120"), std::string::npos);
    EXPECT_NE(dump.find("InsertFinalNewline: true"), std::string::npos);
    EXPECT_NE(dump.find("Keywords:"), std::string::npos);
    // Сгенерированный конфиг можно обратно распарсить без ошибок.
    FormatConfig c = parseConfig(dump);
    EXPECT_TRUE(c.ok) << c.error;
}

TEST(FormatterTest, KeywordActsAsControlKeyword) {
    // Без keywords `each(1, 2)` — без пробела перед '('; с keywords `each` — как ключевое слово.
    const std::string in = "z := each(1, 2);";
    const std::string expNoKw = "z := each(1, 2);\n";
    const std::string expKw = "z := each (1, 2);\n";
    EXPECT_EQ(fmt(in), expNoKw);
    FormatOptions opts;
    opts.keywords = {"each"};
    EXPECT_EQ(fmt(in, opts), expKw);
}

TEST(FormatterTest, DslKeywordNamesFormatAsKeywords) {
    // Имя из дефолтного списка keywords (DSL-макрос) форматируется как ключевое слово:
    // пробел перед '(' (как в clang-format). Форматтер сам загружает дефолтный список
    // keywords из DSL, поэтому 'if' — ключевое слово по умолчанию.
    const std::string in = "if(x){ y:=1; }";
    const std::string exp = "if (x) {\n    y := 1;\n}\n";
    EXPECT_EQ(fmt(in), exp);
    FormatOptions opts;
    opts.keywords = {"if"};
    EXPECT_EQ(fmt(in, opts), exp);
}

} // namespace trust::formatter
