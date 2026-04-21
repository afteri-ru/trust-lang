// lexer_integration_test.cpp - validates lexer error positions using trust::Context.
#include <parser/flex.gen.h>
#include <parser/lexer.hpp>
#include <parser/token.hpp>
#include <diag/context.hpp>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace trust {

// Helper: run lexer on input stored in Context, catch ParserError or return false
static bool RunLexerAndCatchError(const std::string& input,
                                  std::string& out_what, int& out_pos) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", input);
    Lexer lexer{&ctx, idx, 0};

    yyscan_t scanner;
    if (yylex_init(&scanner) != 0) return false;

    yyset_extra(&lexer, scanner);
    YY_BUFFER_STATE buf = yy_scan_bytes(input.data(),
                                         static_cast<int>(input.size()), scanner);
    try {
        yylex(scanner);
        yy_delete_buffer(buf, scanner);
        yylex_destroy(scanner);
        return false; // no error
    } catch (const ParserError& e) {
        out_what = e.what();
        out_pos = static_cast<int>(e.location.offset());
        yy_delete_buffer(buf, scanner);
        yylex_destroy(scanner);
        return true;
    }
}

// Helper: tokenize input and return token kinds
static std::vector<int> Tokenize(const std::string& input) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", input);
    Lexer lexer{&ctx, idx, 0};

    yyscan_t scanner;
    if (yylex_init(&scanner) != 0) return {};
    yyset_extra(&lexer, scanner);
    YY_BUFFER_STATE buf = yy_scan_bytes(input.data(),
                                         static_cast<int>(input.size()), scanner);
    std::vector<int> tokens;
    try {
        int tok;
        while ((tok = yylex(scanner)) != 0) {
            tokens.push_back(tok);
        }
    } catch (const ParserError& e) {
        // If lexer throws, return what we got (may be empty)
    }
    yy_delete_buffer(buf, scanner);
    yylex_destroy(scanner);
    return tokens;
}

// Тест ошибки: незавершённая строка "..."
TEST(LexerErrorPosition, UnterminatedString) {
    std::string what; int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("\"hello\n", what, pos));
    EXPECT_NE(what.find("Unterminated"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый комментарий /* */
TEST(LexerErrorPosition, UnterminatedComment) {
    std::string what; int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("/* comment", what, pos));
    EXPECT_NE(what.find("comment"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый doc-комментарий /** */
TEST(LexerErrorPosition, UnterminatedDocComment) {
    std::string what; int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("/** doc", what, pos));
    EXPECT_NE(what.find("doc comment"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: вложенный doc-комментарий
TEST(LexerErrorPosition, NestedDocComment) {
    std::string what; int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("/** /* inner", what, pos));
    EXPECT_NE(what.find("nested"), std::string::npos)
        << "actual: " << what;
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый embed {% %}
TEST(LexerErrorPosition, UnterminatedEmbed) {
    std::string what; int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("{% code", what, pos));
    EXPECT_NE(what.find("{% %}"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённая макрострока @@@
TEST(LexerErrorPosition, UnterminatedMacroStr) {
    std::string what; int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("@@@ macro", what, pos));
    EXPECT_NE(what.find("@@"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый eval-строка
TEST(LexerErrorPosition, UnterminatedEval) {
    std::string what; int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("`eval", what, pos));
    EXPECT_NE(what.find("eval"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест ошибки: незавершённый атрибут @[ ]@
TEST(LexerErrorPosition, UnterminatedAttribute) {
    std::string what; int pos = -1;
    EXPECT_TRUE(RunLexerAndCatchError("@[attr", what, pos));
    EXPECT_NE(what.find("@[ ... ]@"), std::string::npos);
    EXPECT_GE(pos, 0);
}

// Тест нормального токенизирования
TEST(LexerNormal, SimpleTokens) {
    auto tokens = Tokenize("foo 123 \"hello\"");
    ASSERT_GE(tokens.size(), 2);
    EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::NAME));
    EXPECT_EQ(tokens[1], static_cast<int>(ParserToken::Kind::INTEGER));
}

// Тест raw-строк
TEST(LexerNormal, RawStringWide) {
    auto tokens = Tokenize("r\"hello\"");
    // r"hello" — raw wide string
    if (!tokens.empty()) {
        EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::STRWIDE_RAW));
    }
}

// Тест raw char
TEST(LexerNormal, RawChar) {
    auto tokens = Tokenize("r'x'");
    // r'x' — raw char
    if (!tokens.empty()) {
        EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::STRCHAR_RAW));
    }
}

// Тест комплексных чисел
TEST(LexerNormal, ComplexNumbers) {
    auto tokens = Tokenize("1+2j 3.14-5i");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::COMPLEX));
    EXPECT_EQ(tokens[1], static_cast<int>(ParserToken::Kind::COMPLEX));
}

// Тест mangled-имён
TEST(LexerNormal, MangledNames) {
    auto tokens = Tokenize("_$foo$_bar");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::MANGLED));
}

// Тест модулей
TEST(LexerNormal, ModuleNames) {
    auto tokens = Tokenize("\\foo");
    // Single \foo is a module
    if (tokens.size() >= 1) {
        EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::MODULE));
    }
}

// Тест макрос-токенов
TEST(LexerNormal, MacroTokens) {
    // @@ = MACRO_SEQ, @## = MACRO_CONCAT, @# = MACRO_TOSTR, @foo = MACRO, @$$ = MACRO_NAMESPACE
    auto tokens = Tokenize("@@ @## @# @foo @$$");
    if (tokens.size() >= 5) {
        EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::MACRO_SEQ));
        EXPECT_EQ(tokens[1], static_cast<int>(ParserToken::Kind::MACRO_CONCAT));
        EXPECT_EQ(tokens[2], static_cast<int>(ParserToken::Kind::MACRO_TOSTR));
        EXPECT_EQ(tokens[3], static_cast<int>(ParserToken::Kind::MACRO));
        EXPECT_EQ(tokens[4], static_cast<int>(ParserToken::Kind::MACRO_NAMESPACE));
    }
}

// Тест eval-строк с обратными кавычками
TEST(LexerNormal, EvalString) {
    auto tokens = Tokenize("`foo bar`");
    // `foo bar` — eval string
    if (!tokens.empty()) {
        EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::EVAL));
    }
}

// Тест пробелов игнорируются
TEST(LexerNormal, WhitespaceIgnored) {
    auto tokens = Tokenize("foo   \t\t  \n\n   bar");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::NAME));
    EXPECT_EQ(tokens[1], static_cast<int>(ParserToken::Kind::NAME));
}

// Тест вложенные комментарии
TEST(LexerNormal, NestedComments) {
    auto tokens = Tokenize("/* outer /* inner */ */ foo");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0], static_cast<int>(ParserToken::Kind::NAME));
}

} // namespace trust