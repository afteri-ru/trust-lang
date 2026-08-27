#include "syntax/lexer_test_fixture.hpp"
TEST_F(Lexer, Word) {
    ASSERT_EQ(1, TokenParse("alpha  "));
    EXPECT_EQ(1, Count(TermID::NAME));
    EXPECT_EQ("alpha", tokens[0]->getText());

    ASSERT_EQ(2, TokenParse("буквы    ещёЁ_99"));
    EXPECT_EQ(2, Count(TermID::NAME));
    EXPECT_EQ("буквы", tokens[0]->getText());
    EXPECT_EQ("ещёЁ_99", tokens[1]->getText());

    ASSERT_EQ(3, TokenParse("one two \t three"));
    EXPECT_EQ(3, Count(TermID::NAME));

    EXPECT_EQ("one", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("two", tokens[1]->getText()) << tokens[1]->getText();
    EXPECT_EQ("three", tokens[2]->getText()) << tokens[2]->getText();
}

TEST_F(Lexer, StringEmpty) {
    ASSERT_EQ(1, TokenParse("''"));
    EXPECT_EQ(1, Count(TermID::STRCHAR));
    EXPECT_EQ("", tokens[0]->getText()) << tokens[0]->getText();
}

TEST_F(Lexer, StringEmpty2) {
    ASSERT_EQ(1, TokenParse("\"\""));
    EXPECT_EQ(1, Count(TermID::STRWIDE));
    EXPECT_EQ("", tokens[0]->getText()) << tokens[0]->getText();
}

TEST_F(Lexer, StringSimple) {
    ASSERT_EQ(1, TokenParse("' '"));
    EXPECT_EQ(1, Count(TermID::STRCHAR));
    EXPECT_EQ(" ", tokens[0]->getText()) << tokens[0]->getText();
}

TEST_F(Lexer, StringSimple2) {
    ASSERT_EQ(1, TokenParse("\" \""));
    EXPECT_EQ(1, Count(TermID::STRWIDE));
    EXPECT_EQ(" ", tokens[0]->getText()) << tokens[0]->getText();
}

TEST_F(Lexer, FullString) {
    ASSERT_EQ(1, TokenParse("'  \t \xFF\r\\''"));
    EXPECT_EQ(1, Count(TermID::STRCHAR));
    /* Esc-последовательности больше не декодируются лексером */
    /* Вход: '  \t \xFF\r\' '  → содержимое:   \t \xFF\r\' */
    EXPECT_EQ("  \t \xFF\r\\'", tokens[0]->getText()) << tokens[0]->getText();
}

TEST_F(Lexer, FullString2) {
    ASSERT_EQ(1, TokenParse("\"  \t \xFF\r\\\"\""));
    EXPECT_EQ(1, Count(TermID::STRWIDE));
    /* Esc-последовательности больше не декодируются лексером */
    /* Вход: "  \t \xFF\r\" "  → содержимое:   \t \xFF\r\" */
    EXPECT_EQ("  \t \xFF\r\\\"", tokens[0]->getText()) << tokens[0]->getText();
}

TEST_F(Lexer, Integer) {
    ASSERT_EQ(1, TokenParse("123456"));
    EXPECT_EQ(1, Count(TermID::INTEGER)) << trust::toString(tokens[0]->getTermID());

    EXPECT_EQ("123456", tokens[0]->getText());

    ASSERT_EQ(3, TokenParse("123456 * 123"));
    EXPECT_EQ(1, Count(TermID::STAR)) << Dump();
    EXPECT_EQ(2, Count(TermID::INTEGER)) << Dump();

    EXPECT_EQ("123456", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("*", tokens[1]->getText()) << tokens[1]->getText();
    EXPECT_EQ("123", tokens[2]->getText()) << tokens[2]->getText();
}

TEST_F(Lexer, Float) {
    ASSERT_EQ(1, TokenParse("1.e10"));
    EXPECT_EQ(1, Count(TermID::NUMBER));
    EXPECT_EQ("1.e10", tokens[0]->getText());
}

TEST_F(Lexer, Complex0) {
    if (1 != TokenParse("1j")) {
        for (auto elem : tokens) {
            std::cout << trust::toString(elem->m_id) << " " << elem->getText() << "\n";
        }
    }
    ASSERT_EQ(1, tokens.size());
    EXPECT_EQ(1, Count(TermID::COMPLEX)) << trust::toString(tokens[0]->m_id);
    EXPECT_EQ("1j", tokens[0]->getText());

    if (1 != TokenParse("-1i")) {
        for (auto elem : tokens) {
            std::cout << trust::toString(elem->m_id) << " " << elem->getText() << "\n";
        }
    }
    ASSERT_EQ(1, tokens.size());
    EXPECT_EQ(1, Count(TermID::COMPLEX)) << trust::toString(tokens[0]->m_id);
    EXPECT_EQ("-1i", tokens[0]->getText());

    if (1 != TokenParse("-1j-0.2")) {
        for (auto elem : tokens) {
            std::cout << trust::toString(elem->m_id) << " " << elem->getText() << "\n";
        }
    }
    ASSERT_EQ(1, tokens.size());
    EXPECT_EQ(1, Count(TermID::COMPLEX)) << trust::toString(tokens[0]->m_id);
    EXPECT_EQ("-1j-0.2", tokens[0]->getText());

    if (1 != TokenParse("1i+2")) {
        for (auto elem : tokens) {
            std::cout << trust::toString(elem->m_id) << " " << elem->getText() << "\n";
        }
    }
    ASSERT_EQ(1, tokens.size());
    EXPECT_EQ(1, Count(TermID::COMPLEX)) << trust::toString(tokens[0]->m_id);
    EXPECT_EQ("1i+2", tokens[0]->getText());

    if (1 != TokenParse("1j+0.2")) {
        for (auto elem : tokens) {
            std::cout << trust::toString(elem->m_id) << " " << elem->getText() << "\n";
        }
    }
    ASSERT_EQ(1, tokens.size());
    EXPECT_EQ(1, Count(TermID::COMPLEX)) << trust::toString(tokens[0]->m_id);
    EXPECT_EQ("1j+0.2", tokens[0]->getText());
}

TEST_F(Lexer, Complex1) {
    if (1 != TokenParse("1.333+0.e10j")) {
        for (auto elem : tokens) {
            std::cout << trust::toString(elem->m_id) << " " << elem->getText() << "\n";
        }
    }
    ASSERT_EQ(1, tokens.size());
    EXPECT_EQ(1, Count(TermID::COMPLEX));
    EXPECT_EQ("1.333+0.e10j", tokens[0]->getText());
}

TEST_F(Lexer, Term) {

    if (1 != TokenParse("$alpha  ")) {
        for (auto elem : tokens) {
            std::cout << trust::toString(elem->m_id) << " " << elem->getText() << "\n";
        }
    }
    ASSERT_EQ(1, tokens.size());
    EXPECT_EQ(1, Count(TermID::LOCAL)) << Dump();
    EXPECT_EQ("$alpha", tokens[0]->getText());

    ASSERT_EQ(2, TokenParse("буквы    ещёЁ_99"));
    EXPECT_EQ(2, Count(TermID::NAME));
    EXPECT_EQ("буквы", tokens[0]->getText());
    EXPECT_EQ("ещёЁ_99", tokens[1]->getText());

    ASSERT_EQ(5, TokenParse("one \\two \\\\two \t $three @four")) << Dump();
    EXPECT_EQ(1, Count(TermID::NAME)) << Dump();
    EXPECT_EQ(1, Count(TermID::LOCAL)) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO)) << Dump();
    EXPECT_EQ(2, Count(TermID::MODULE)) << Dump();

    EXPECT_EQ("one", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("\\two", tokens[1]->getText()) << tokens[1]->getText();
    EXPECT_EQ("\\\\two", tokens[2]->getText()) << tokens[2]->getText();
    EXPECT_EQ("$three", tokens[3]->getText()) << tokens[3]->getText();
    EXPECT_EQ("@four", tokens[4]->getText()) << tokens[4]->getText();
}

TEST_F(Lexer, AssignEq) {
    ASSERT_EQ(3, TokenParse("token=ssssssss"));
    EXPECT_EQ(2, Count(TermID::NAME));
    EXPECT_EQ(1, Count(TermID::EQ));

    EXPECT_EQ("token", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("ssssssss", tokens[2]->getText()) << tokens[2]->getText();

    ASSERT_EQ(3, TokenParse("token:=\"ssssssss\""));
    EXPECT_EQ(1, Count(TermID::NAME));
    EXPECT_EQ(1, Count(TermID::CREATE_NAME));
    EXPECT_EQ(1, Count(TermID::STRWIDE));

    EXPECT_EQ("token", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("ssssssss", tokens[2]->getText()) << tokens[2]->getText();

    ASSERT_EQ(3, TokenParse("    token   \t  ::=   'ssssssss'       "));
    EXPECT_EQ(1, Count(TermID::NAME));
    EXPECT_EQ(1, Count(TermID::CREATE_TYPE));
    EXPECT_EQ(1, Count(TermID::STRCHAR));

    EXPECT_EQ("token", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("ssssssss", tokens[2]->getText()) << tokens[2]->getText();
}

TEST_F(Lexer, CodeInner) {
    ASSERT_EQ(3, TokenParse("{%if(){%}   {%}else{%}   {%} %}"));
    EXPECT_EQ(3, Count(TermID::EMBED));
    EXPECT_EQ("if(){", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("}else{", tokens[1]->getText()) << tokens[1]->getText();
    EXPECT_EQ("} ", tokens[2]->getText()) << tokens[2]->getText();

    ASSERT_EQ(5, TokenParse("{ {%if(){%}   {%}else{%}   {%} %} }"));
    EXPECT_EQ(1, Count(TermID::LBRACE));
    EXPECT_EQ(1, Count(TermID::RBRACE));
    EXPECT_EQ(3, Count(TermID::EMBED));
    EXPECT_EQ("{", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("if(){", tokens[1]->getText()) << tokens[1]->getText();
    EXPECT_EQ("}else{", tokens[2]->getText()) << tokens[2]->getText();
    EXPECT_EQ("} ", tokens[3]->getText()) << tokens[3]->getText();
    EXPECT_EQ("}", tokens[4]->getText()) << tokens[4]->getText();
}

TEST_F(Lexer, Code) {
    ASSERT_EQ(2, TokenParse("{            }"));
    EXPECT_EQ(1, Count(TermID::LBRACE));
    EXPECT_EQ(1, Count(TermID::RBRACE));
    EXPECT_EQ("{", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("}", tokens[1]->getText()) << tokens[1]->getText();

    ASSERT_EQ(4, TokenParse("{ { } }"));
    EXPECT_EQ(2, Count(TermID::LBRACE));
    EXPECT_EQ(2, Count(TermID::RBRACE));
}

TEST_F(Lexer, CodeSource) {
    ASSERT_EQ(1, TokenParse("{%%}"));
    EXPECT_EQ(1, Count(TermID::EMBED));
    EXPECT_EQ("", tokens[0]->getText()) << tokens[0]->getText();

    ASSERT_EQ(1, TokenParse("{% % %}"));
    ASSERT_EQ(1, Count(TermID::EMBED));
    ASSERT_EQ(" % ", tokens[0]->getText()) << tokens[0]->getText();
}

TEST_F(Lexer, Function) {
    ASSERT_EQ(1, TokenParse("\\name")) << Dump();
    EXPECT_EQ(1, Count(TermID::MODULE)) << toString(tokens[0]->getTermID());
    EXPECT_EQ("\\name", tokens[0]->getText()) << tokens[0]->getText();

    ASSERT_EQ(1, TokenParse("\\\\name"));
    EXPECT_EQ(1, Count(TermID::MODULE)) << toString(tokens[0]->getTermID());
    EXPECT_EQ("\\\\name", tokens[0]->getText()) << tokens[0]->getText();

    ASSERT_EQ(1, TokenParse("$name"));
    EXPECT_EQ(1, Count(TermID::LOCAL)) << toString(tokens[0]->getTermID());
    EXPECT_EQ("$name", tokens[0]->getText()) << tokens[0]->getText();

    ASSERT_EQ(2, TokenParse("%native"));
    EXPECT_EQ(1, Count(TermID::PERCENT)) << toString(tokens[0]->getTermID());
    EXPECT_EQ(1, Count(TermID::NAME)) << toString(tokens[1]->getTermID());
    EXPECT_EQ("%", tokens[0]->getText()) << tokens[0]->getText();
    EXPECT_EQ("native", tokens[1]->getText()) << tokens[1]->getText();

    ASSERT_EQ(1, TokenParse("@name"));
    EXPECT_EQ(1, Count(TermID::MACRO));
    EXPECT_EQ("@name", tokens[0]->getText()) << tokens[0]->getText();

    ASSERT_EQ(1, TokenParse("@функция_alpha_ёЁ"));
    EXPECT_EQ(1, Count(TermID::MACRO));
    EXPECT_EQ("@функция_alpha_ёЁ", tokens[0]->getText()) << tokens[0]->getText();
}

TEST_F(Lexer, Sentence) {
    ASSERT_EQ(2, TokenParse("token."));
    EXPECT_EQ(1, Count(TermID::NAME));
    ASSERT_EQ(2, TokenParse("token;"));
    EXPECT_EQ(1, Count(TermID::NAME));
}

TEST_F(Lexer, Paren) {
    ASSERT_EQ(3, TokenParse("\\name()")) << Dump();
    EXPECT_EQ(1, Count(TermID::MODULE));
    EXPECT_EQ(1, Count(TermID::LPAREN));
    EXPECT_EQ(1, Count(TermID::RPAREN));

    ASSERT_EQ(4, TokenParse("%функция_alpha_ёЁ ()"));
    EXPECT_EQ(1, Count(TermID::NAME));
    EXPECT_EQ(3, Count(TermID::PERCENT) + Count(TermID::LPAREN) + Count(TermID::RPAREN));
}

TEST_F(Lexer, Module) {
    ASSERT_EQ(1, TokenParse("\\name")) << Dump();
    EXPECT_EQ(1, Count(TermID::MODULE));

    ASSERT_EQ(1, TokenParse("\\\\dir\\module"));
    EXPECT_EQ(1, Count(TermID::MODULE));

    ASSERT_EQ(1, TokenParse("\\dir\\dir\\module"));
    EXPECT_EQ(1, Count(TermID::MODULE));

    ASSERT_EQ(3, TokenParse("\\name::var")) << Dump();
    EXPECT_EQ(1, Count(TermID::MODULE));

    ASSERT_EQ(5, TokenParse("\\\\dir\\module::var.filed")) << Dump();
    EXPECT_EQ(1, Count(TermID::MODULE));

    ASSERT_EQ(5, TokenParse("\\dir\\dir\\module::var.filed")) << Dump();
    EXPECT_EQ(1, Count(TermID::MODULE));
}

TEST_F(Lexer, Arg) {
    ASSERT_EQ(7, TokenParse("term(name=value);"));
    EXPECT_EQ(3, Count(TermID::NAME));
    EXPECT_EQ(4, Count(TermID::LPAREN) + Count(TermID::EQ) + Count(TermID::RPAREN) + Count(TermID::SEMICOLON));
}

TEST_F(Lexer, Args) {
    ASSERT_EQ(11, TokenParse("$0 $1 $22 $333 $4sss $sss1 -- ++ $* $^  ")) << Dump();
    EXPECT_EQ(5, Count(TermID::ARGUMENT)) << Dump();
    EXPECT_EQ(2, Count(TermID::ARGS)) << Dump();
    EXPECT_EQ(1, Count(TermID::INT_PLUS)) << Dump();
    EXPECT_EQ(1, Count(TermID::INT_MINUS)) << Dump();
    EXPECT_EQ(1, Count(TermID::NAME)) << Dump();
    EXPECT_EQ(1, Count(TermID::LOCAL)) << Dump();
}

TEST_F(Lexer, MutArgs) {
    ASSERT_EQ(11, TokenParse("$0^ $1^ $22 $333 $4sss^ $sss1^ -- ++ $* $^  ")) << Dump();
    EXPECT_EQ(5, Count(TermID::ARGUMENT)) << Dump();
    EXPECT_EQ(2, Count(TermID::ARGS)) << Dump();
    EXPECT_EQ(1, Count(TermID::INT_PLUS)) << Dump();
    EXPECT_EQ(1, Count(TermID::INT_MINUS)) << Dump();
    EXPECT_EQ(1, Count(TermID::NAME)) << Dump();
    EXPECT_EQ(1, Count(TermID::LOCAL)) << Dump();
}

// TEST_F(Lexer, MutLink) {
//     ASSERT_EQ(11, TokenParse("&0^ &1^ &22 &333 &4sss^ &sss1^ -- ++ &* &^  ")) << Dump();
//     EXPECT_EQ(5, Count(TermID::ARGUMENT)) << Dump();
//     EXPECT_EQ(2, Count(TermID::ARGS)) << Dump();
//     EXPECT_EQ(1, Count(TermID::INT_PLUS)) << Dump();
//     EXPECT_EQ(1, Count(TermID::INT_MINUS)) << Dump();
//     EXPECT_EQ(1, Count(TermID::NAME)) << Dump();
//     EXPECT_EQ(1, Count(TermID::LOCAL)) << Dump();
// }

/* @{      }@
 * {@      @}
 *
 * SyncNone
 * SyncTimedMutex
 * SyncTimedShared
 * SyncSingleThread
 *
 * {@ &0 ::=  SyncNone  @};                 # *
 * {@ &1 ::=  SyncSingleThread  @};         # &
 * {@ &2 ::=  SyncTimedShared  @};          # &&
 * {@ &3 ::=  SyncTimedSharedRecursive @};  # &*
 *
 * [@ unused @]
 * [@ unsafe(on) @]
 * [@ warning(no=100, message="ddddddddd") @]
 *
 * {@ func.unused ::=  1 @};  # &*
 *
 */
TEST_F(Lexer, UTF8) {
    ASSERT_EQ(7, TokenParse("термин(имя=значение);"));
    EXPECT_EQ(3, Count(TermID::NAME)) << Dump();
    EXPECT_EQ(4, Count(TermID::LPAREN) + Count(TermID::EQ) + Count(TermID::RPAREN) + Count(TermID::SEMICOLON)) << Dump();
}

TEST_F(Lexer, MutUTF8) {
    ASSERT_EQ(7, TokenParse("термин^(имя^=значение);"));
    EXPECT_EQ(3, Count(TermID::NAME)) << Dump();
    EXPECT_EQ(4, Count(TermID::LPAREN) + Count(TermID::EQ) + Count(TermID::RPAREN) + Count(TermID::SEMICOLON)) << Dump();
    ASSERT_EQ(7, TokenParse("термин^(имя^=значение);"));
    EXPECT_EQ(3, Count(TermID::NAME)) << Dump();
    EXPECT_EQ(4, Count(TermID::LPAREN) + Count(TermID::EQ) + Count(TermID::RPAREN) + Count(TermID::SEMICOLON)) << Dump();
}

TEST_F(Lexer, ELLIPSIS) {
    ASSERT_EQ(2, TokenParse("... ...")) << Dump();
    EXPECT_EQ(2, Count(TermID::ELLIPSIS)) << Dump();
}

TEST_F(Lexer, Alias) {
    ASSERT_EQ(5, TokenParse("+>:<-")) << Dump();
    EXPECT_EQ(5, Count(TermID::PLUS) + Count(TermID::GT) + Count(TermID::COLON) + Count(TermID::LT) + Count(TermID::MINUS)) << Dump();

    ASSERT_EQ(4, TokenParse("@alias := @ALIAS;")) << Dump();
    EXPECT_EQ(2, Count(TermID::MACRO)) << Dump();

    ASSERT_EQ(7, TokenParse("/** Comment */@@   alias2   @@      ALIAS2 @@@@///< Комментарий")) << Dump();
    EXPECT_EQ(2, Count(TermID::DOCUMENT));
    EXPECT_EQ(2, Count(TermID::NAME));
    EXPECT_FALSE(tokens[0]->m_mapperRange.begin.isInvalid()) << Dump();
    EXPECT_FALSE(tokens[1]->m_mapperRange.begin.isInvalid()) << Dump();

    ASSERT_EQ(2, TokenParse("/** Русские символы */name")) << Dump();
    EXPECT_EQ(1, Count(TermID::DOCUMENT));
    EXPECT_EQ(1, Count(TermID::NAME));
    EXPECT_FALSE(tokens[0]->m_mapperRange.begin.isInvalid());
    EXPECT_FALSE(tokens[1]->m_mapperRange.begin.isInvalid());
}
