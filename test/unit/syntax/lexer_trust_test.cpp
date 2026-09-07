#include "syntax/lexer_test_fixture.hpp"
TEST_F(Lexer, TrustMarkers) {
    ASSERT_GT(TokenParse("@{ a @} @{ c @} @{ d @}"), 0u);
    ASSERT_EQ(9u, tokens.size());
    EXPECT_EQ(TermID::TRUST_BEGIN, tokens[0]->getTermID());
    EXPECT_EQ(TermID::NAME, tokens[1]->getTermID());
    EXPECT_EQ(TermID::TRUST_END, tokens[2]->getTermID());
    EXPECT_EQ(TermID::TRUST_BEGIN, tokens[3]->getTermID());
    EXPECT_EQ(TermID::TRUST_END, tokens[5]->getTermID());
    EXPECT_EQ(TermID::TRUST_BEGIN, tokens[6]->getTermID());
    EXPECT_EQ(TermID::TRUST_END, tokens[8]->getTermID());
}

TEST_F(Lexer, TrustMarkersExpressionContent) {
    // Содержимое - логическое выражение: операторы, вызов, скобки.
    ASSERT_GT(TokenParse("@{ a > 0 @} @{ f(x) @}"), 0u);
    // @{ a > 0 @}: TRUST_BEGIN NAME GT INTEGER TRUST_END
    EXPECT_EQ(TermID::TRUST_BEGIN, tokens[0]->getTermID());
    EXPECT_EQ(TermID::GT, tokens[2]->getTermID());
    EXPECT_EQ(TermID::TRUST_END, tokens[4]->getTermID());
    // @{ f(x) @}: TRUST_BEGIN NAME LPAREN NAME RPAREN TRUST_END
    EXPECT_EQ(TermID::TRUST_BEGIN, tokens[5]->getTermID());
    EXPECT_EQ(TermID::LPAREN, tokens[7]->getTermID());
    EXPECT_EQ(TermID::TRUST_END, tokens[10]->getTermID());
}

TEST_F(Lexer, TrustElemLexer) {
    ASSERT_GT(TokenParse("@( old, x @)"), 0u);
    EXPECT_EQ(TermID::TRUST_ELEM_BEGIN, tokens[0]->getTermID());
    EXPECT_EQ(TermID::TRUST_ELEM_END, tokens[tokens.size() - 1]->getTermID());
}
