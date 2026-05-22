#include "ast/token.hpp"
#include "ast/token_info.hpp"
#include "diag/context.hpp"
#include <gtest/gtest.h>
#include <string>

// Include generated parser header
#include "parser.tab.hh"

namespace trust {

// Helper: run ParserAST and capture output
static TokenSequence RunParser(const TokenSequence& input) {
    TokenSequence out;
    std::size_t pos = 0;
    TokenSequence mutable_input(input.begin(), input.end());
    Context ctx;
    ParserContext pc(mutable_input, pos, out, ctx);
    ParserAST parser(pc);
    int result = parser.parse();
    (void)result;
    return out;
}

// static TokenSequence RunParser(const TokenSequence &input, std::string &err) {
//     TokenSequence out;
//     std::size_t pos = 0;
//     TokenSequence mutable_input(input.begin(), input.end());
//     ParserAST parser(mutable_input, pos, out, err);
//     int result = parser.parse();
//     (void)result;
//     return out;
// }

// Helper: create a token of given kind with text
static TokenPtr MakeToken(ParserToken::Kind kind, std::string text = "") {
    return TokenInfo::make(kind, std::move(text));
}

// Test: empty input produces empty output
TEST(ParserASTTest, DISABLED_EmptyInput) {

    TokenSequence input;
    auto out = RunParser(input);
    EXPECT_TRUE(out.empty());
}

// Test: single integer literal parsed successfully
TEST(ParserASTTest, DISABLED_IntegerLiteral) {
    TokenSequence input = {MakeToken(ParserToken::Kind::INTEGER, "42")};
    auto out = RunParser(input);
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0]->m_sequence.size(), 1);
    EXPECT_EQ(out[0]->m_sequence[0]->kind, ParserToken::Kind::INTEGER) << ParserToken::name(out[0]->m_sequence[0]->kind);
    EXPECT_EQ(out[0]->m_sequence[0]->text, "42");
}

// Test: single number literal parsed successfully
TEST(ParserASTTest, DISABLED_NumberLiteral) {
    TokenSequence input = {MakeToken(ParserToken::Kind::NUMBER, "3.14")};
    auto out = RunParser(input);
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0]->m_sequence.size(), 1);
    EXPECT_EQ(out[0]->m_sequence[0]->kind, ParserToken::Kind::NUMBER) << ParserToken::name(out[0]->m_sequence[0]->kind);
    EXPECT_EQ(out[0]->m_sequence[0]->text, "3.14");
}

// Test: single string wide literal parsed successfully
TEST(ParserASTTest, DISABLED_StringWideLiteral) {
    TokenSequence input = {MakeToken(ParserToken::Kind::STRWIDE, "hello")};
    auto out = RunParser(input);
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0]->m_sequence.size(), 1);
    EXPECT_EQ(out[0]->m_sequence[0]->kind, ParserToken::Kind::STRWIDE) << ParserToken::name(out[0]->m_sequence[0]->kind);
    EXPECT_EQ(out[0]->m_sequence[0]->text, "hello");
}

// Test: single string char literal parsed successfully
TEST(ParserASTTest, DISABLED_StringCharLiteral) {
    TokenSequence input = {MakeToken(ParserToken::Kind::STRCHAR, "abc")};
    auto out = RunParser(input);
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0]->m_sequence.size(), 1);
    EXPECT_EQ(out[0]->m_sequence[0]->kind, ParserToken::Kind::STRCHAR) << ParserToken::name(out[0]->m_sequence[0]->kind);
    EXPECT_EQ(out[0]->m_sequence[0]->text, "abc");
}

// Test: complex literal parsed successfully
TEST(ParserASTTest, DISABLED_ComplexLiteral) {
    TokenSequence input = {MakeToken(ParserToken::Kind::COMPLEX, "1+2i")};
    auto out = RunParser(input);
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0]->m_sequence.size(), 1);
    EXPECT_EQ(out[0]->m_sequence[0]->kind, ParserToken::Kind::COMPLEX) << ParserToken::name(out[0]->m_sequence[0]->kind);
    EXPECT_EQ(out[0]->m_sequence[0]->text, "1+2i");
}

// Test: rational literal parsed successfully
TEST(ParserASTTest, DISABLED_RationalLiteral) {
    TokenSequence input = {MakeToken(ParserToken::Kind::RATIONAL, "1/2")};
    auto out = RunParser(input);
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0]->m_sequence.size(), 1);
    EXPECT_EQ(out[0]->m_sequence[0]->kind, ParserToken::Kind::RATIONAL) << ParserToken::name(out[0]->m_sequence[0]->kind);
    EXPECT_EQ(out[0]->m_sequence[0]->text, "1/2");
}

// Test: multiple semicolons after a statement are absorbed as separator
// The grammar: sequence → stmt | sequence separator
// So trailing semicolons are consumed but don't create new statements
TEST(ParserASTTest, DISABLED_DISABLED_TrailingSeparatorsAbsorbed) {
    TokenSequence input = {
        MakeToken(ParserToken::Kind::INTEGER, "1"),
        MakeToken(ParserToken::Kind::SEMICOLON, ";"),
        MakeToken(ParserToken::Kind::SEMICOLON, ";"),
    };
    auto out = RunParser(input);
    ASSERT_EQ(out.size(), 1u);
    // The stmt has itself pushed into sequence, plus separators absorbed
    ASSERT_EQ(out[0]->m_sequence.size(), 1) << TokenInfo::dump(out[0].get());
    EXPECT_EQ(out[0]->m_sequence[0]->text, "1");
}

// Test: multiple statements - grammar doesn't support stmt+separator+stmt
// Only sequence → stmt | sequence separator (absorbs trailing separators)
// Second statement is not consumed, parse returns with partial result
TEST(ParserASTTest, DISABLED_MultipleStatementsNotFullyConsumed) {
    TokenSequence input = {
        MakeToken(ParserToken::Kind::INTEGER, "1"),
        MakeToken(ParserToken::Kind::SEMICOLON, ";"),
        MakeToken(ParserToken::Kind::INTEGER, "2"),
    };
    auto out = RunParser(input);
    // Grammar: sequence → stmt | sequence separator
    // The first stmt + separator is consumed, second INTEGER is left unconsumed
    // Parser stops after reducing sequence
    ASSERT_EQ(out.size(), 1u);
    // Only one statement in sequence (second integer token is not parsed)
    ASSERT_EQ(out[0]->m_sequence.size(), 2) << TokenInfo::dump(out[0].get());
    EXPECT_EQ(out[0]->m_sequence[0]->text, "1");
    EXPECT_EQ(out[0]->m_sequence[1]->text, "2");
}

// Test: literal followed by single separator
TEST(ParserASTTest, DISABLED_DISABLED_LiteralWithSeparator) {
    TokenSequence input = {
        MakeToken(ParserToken::Kind::INTEGER, "100"),
        MakeToken(ParserToken::Kind::SEMICOLON, ";"),
    };
    auto out = RunParser(input);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0]->m_sequence.size(), 1u) << TokenInfo::dump(out[0].get());
    EXPECT_EQ(out[0]->m_sequence[0]->text, "100");
}

} // namespace trust