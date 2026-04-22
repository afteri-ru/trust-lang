// lexer_integration_test.cpp - validates lexer error positions using trust::Context.
#include <parser/lexer.hpp>
#include <diag/context.hpp>
#include <gtest/gtest.h>
#include <string>

namespace trust {

// Helper: run lexer on input stored in Context, catch ParserError or return false
static bool RunLexerAndCatchError(const std::string &input, std::string &out_what, int &out_pos) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", input);
    try {
        Lexer::tokenize(ctx, idx);
        return false; // no error
    } catch (const ParserError &e) {
        out_what = e.what();
        out_pos = static_cast<int>(e.location.offset());
        return true;
    }
}

// Тест ошибки: незавершённая строка "..."
TEST(LexerErrorPosition, UnterminatedString) {
    std::string what;
    int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("\"hello\n", what, pos));
    EXPECT_NE(what.find("Unterminated"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый комментарий /* */
TEST(LexerErrorPosition, UnterminatedComment) {
    std::string what;
    int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("/* comment", what, pos));
    EXPECT_NE(what.find("comment"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый doc-комментарий /** */
TEST(LexerErrorPosition, UnterminatedDocComment) {
    std::string what;
    int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("/** doc", what, pos));
    EXPECT_NE(what.find("doc comment"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: вложенный doc-комментарий
TEST(LexerErrorPosition, NestedDocComment) {
    std::string what;
    int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("/** /* inner", what, pos));
    EXPECT_NE(what.find("nested"), std::string::npos) << "actual: " << what;
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый embed {% %}
TEST(LexerErrorPosition, UnterminatedEmbed) {
    std::string what;
    int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("{% code", what, pos));
    EXPECT_NE(what.find("{% %}"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённая макрострока @@@
TEST(LexerErrorPosition, UnterminatedMacroStr) {
    std::string what;
    int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("@@@ macro", what, pos));
    EXPECT_NE(what.find("@@"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый eval-строка
TEST(LexerErrorPosition, UnterminatedEval) {
    std::string what;
    int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("`eval", what, pos));
    EXPECT_NE(what.find("eval"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый атрибут @[ ]@
TEST(LexerErrorPosition, UnterminatedAttribute) {
    std::string what;
    int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("@[attr", what, pos));
    EXPECT_NE(what.find("@[ ... ]@"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест нормального токенизирования
TEST(LexerNormal, SimpleTokens) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "foo 123 \"hello\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 2);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::NAME);
    EXPECT_EQ(lexemes[1].kind, ParserToken::Kind::INTEGER);
}

// Тест raw-строк
TEST(LexerNormal, RawStringWide) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "r\"hello\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    // r"hello" — raw wide string
    if (!lexemes.empty()) {
        EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::STRWIDE_RAW);
    }
}

// Тест raw char
TEST(LexerNormal, RawChar) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "r'x'");
    auto lexemes = Lexer::tokenize(ctx, idx);
    // r'x' — raw char
    if (!lexemes.empty()) {
        EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::STRCHAR_RAW);
    }
}

// Тест комплексных чисел
TEST(LexerNormal, ComplexNumbers) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "1+2j 3.14-5i");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::COMPLEX);
    EXPECT_EQ(lexemes[1].kind, ParserToken::Kind::COMPLEX);
}

// Тест mangled-имён
TEST(LexerNormal, MangledNames) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "_$foo$_bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::MANGLED);
}

// Тест модулей
TEST(LexerNormal, ModuleNames) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "\\foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    // Single \foo is a module
    if (lexemes.size() >= 1) {
        EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::MODULE);
    }
}

// Тест макрос-токенов
TEST(LexerNormal, MacroTokens) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "@@ @## @# @foo @$$");
    auto lexemes = Lexer::tokenize(ctx, idx);
    // @@ = MACRO_SEQ, @## = MACRO_CONCAT, @# = MACRO_TOSTR, @foo = MACRO, @$$ = MACRO_NAMESPACE
    if (lexemes.size() >= 5) {
        EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::MACRO_SEQ);
        EXPECT_EQ(lexemes[1].kind, ParserToken::Kind::MACRO_CONCAT);
        EXPECT_EQ(lexemes[2].kind, ParserToken::Kind::MACRO_TOSTR);
        EXPECT_EQ(lexemes[3].kind, ParserToken::Kind::MACRO);
        EXPECT_EQ(lexemes[4].kind, ParserToken::Kind::MACRO_NAMESPACE);
    }
}

// Тест eval-строк с обратными кавычками
TEST(LexerNormal, EvalString) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "`foo bar`");
    auto lexemes = Lexer::tokenize(ctx, idx);
    // `foo bar` — eval string
    ASSERT_EQ(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::EVAL);
}

// Тест пробелов игнорируются
TEST(LexerNormal, WhitespaceIgnored) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "foo   \t\t  \n\n   bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::NAME);
    EXPECT_EQ(lexemes[1].kind, ParserToken::Kind::NAME);
}

// Тест вложенные комментарии
TEST(LexerNormal, NestedComments) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "/* outer /* inner */ */ foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::NAME);
}

// ============================================================
// EMBED/EVAL/ATTRIBUTE/MACRO_STR — проверка создания токенов
// ============================================================

TEST(LexerMultilineTokens, Embed) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "{% code %}");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::EMBED);
    EXPECT_EQ(lexemes[0], " code ");
}

TEST(LexerMultilineTokens, Eval) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "`foo bar`");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::EVAL);
    EXPECT_EQ(lexemes[0], "foo bar");
}

TEST(LexerMultilineTokens, Attribute) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "@[attr]@");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::ATTRIBUTE);
    EXPECT_EQ(lexemes[0], "attr");
}

TEST(LexerMultilineTokens, MacroStr) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "@@@ text @@@");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::MACRO_STR);
    EXPECT_EQ(lexemes[0], " text ");
}

// String tokens now produce tokens via TK_STATEFUL
TEST(LexerMultilineTokens, StringsProduceTokens) {
    Context ctx;
    {
        auto idx = ctx.add_source("<test>", "\"hello\"");
        auto lexemes = Lexer::tokenize(ctx, idx);
        ASSERT_EQ(lexemes.size(), 1);
        EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::STRWIDE);
    }

    {
        auto idx = ctx.add_source("<test>", "'x'");
        auto lexemes = Lexer::tokenize(ctx, idx);
        ASSERT_EQ(lexemes.size(), 1);
        EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::STRCHAR);
    }

    {
        auto idx = ctx.add_source("<test>", "r\"hello\"");
        auto lexemes = Lexer::tokenize(ctx, idx);
        ASSERT_EQ(lexemes.size(), 1);
        EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::STRWIDE_RAW);
    }

    {
        auto idx = ctx.add_source("<test>", "r'x'");
        auto lexemes = Lexer::tokenize(ctx, idx);
        ASSERT_EQ(lexemes.size(), 1);
        EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::STRCHAR_RAW);
    }
}

// ============================================================
// Документирующие комментарии СОЗДАЮТ токены
// ============================================================

TEST(LexerDocComments, SingleLineDocBefore) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "/// doc comment");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::DOC_BEFORE);
}

TEST(LexerDocComments, SingleLineDocAfter) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "///< doc after");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::DOC_AFTER);
}

TEST(LexerDocComments, MultiLineDocComment) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "/** doc comment */");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::DOC_BEFORE);
    EXPECT_EQ(lexemes[0], " doc comment ") << lexemes[0].begin();
}

TEST(LexerDocComments, SingleLineDocBeforeHash) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "## doc comment");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::DOC_BEFORE);
}

TEST(LexerDocComments, SingleLineDocAfterHash) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "##< doc after");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::DOC_AFTER);
}

TEST(LexerDocComments, EmptyDocComment) {
    // "/**/" — пустой doc-комментарий, поглощается без токена
    Context ctx;
    auto idx = ctx.add_source("<test>", "/**/");
    auto lexemes = Lexer::tokenize(ctx, idx);
    EXPECT_EQ(lexemes.size(), 0);
}

// ============================================================
// Обычные комментарии НЕ создают токенов
// ============================================================

TEST(LexerNoTokens, RegularComment) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "/* regular comment */");
    auto lexemes = Lexer::tokenize(ctx, idx);
    EXPECT_EQ(lexemes.size(), 0);
}

TEST(LexerNoTokens, SingleLineHashComment) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "# single line comment");
    auto lexemes = Lexer::tokenize(ctx, idx);
    EXPECT_EQ(lexemes.size(), 0);
}

TEST(LexerNoTokens, CommentBeforeCode) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "/* comment */ foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::NAME);
}

// ============================================================
// Пробелы НЕ создают токенов
// ============================================================

TEST(LexerNoTokens, WhitespaceOnly) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "   \t\n\t   ");
    auto lexemes = Lexer::tokenize(ctx, idx);
    EXPECT_EQ(lexemes.size(), 0);
}

TEST(LexerNoTokens, WhitespaceAroundCode) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "   foo   \t\n   bar   ");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::NAME);
    EXPECT_EQ(lexemes[1].kind, ParserToken::Kind::NAME);
}

// ============================================================
// Многострочные комментарии обработаны корректно
// ============================================================

TEST(LexerMultilineComments, NestedBlockComments) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "/* outer /* inner /* deepest */ */ */ foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::NAME);
}

TEST(LexerMultilineComments, EmptyBlockComment) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "/**/ foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::NAME);
}

TEST(LexerMultilineComments, DocCommentWithNewlines) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "/** line1\nline2 */");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::DOC_BEFORE);
}

// ============================================================
// Multiline EMBED/EVAL/ATTRIBUTE/MACRO_STR с новыми строками
// ============================================================

TEST(LexerMultilineTokens, EmbedMultiline) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "{%\nline1\nline2\n%}");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::EMBED);
}

TEST(LexerMultilineTokens, EvalUnterminatedOnNewline) {
    std::string what;
    int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("`line1\nline2`", what, pos));
    EXPECT_NE(what.find("eval"), std::string::npos);
}

TEST(LexerMultilineTokens, AttributeMultiline) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "@[\nattr\n]@");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_GE(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::ATTRIBUTE);
}

TEST(LexerMultilineTokens, MacroStrMultiline) {
    Context ctx;
    auto idx = ctx.add_source("<test>", "@@@\nline1\nline2\n@@@");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1);
    EXPECT_EQ(lexemes[0].kind, ParserToken::Kind::MACRO_STR);
    EXPECT_EQ(lexemes[0], "\nline1\nline2\n");
}

} // namespace trust
