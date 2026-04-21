// lexer_test.cpp - validates lexer token generation using GTest.
#include "parser/token.hpp"
#include <gtest/gtest.h>
#include <set>
#include <string>

namespace trust {

// All FLEX_LEXEME tokens must exist and have correct properties
TEST(LexerTokens, AllFlexLexemesHaveUpperNames) {
    for (auto k : ParserToken::all()) {
        if (ParserToken::is_flex_lexeme(k)) {
            std::string name(ParserToken::name(k));
            for (char c : name) {
                if (c >= 'a' && c <= 'z') {
                    FAIL() << "FLEX_LEXEME token '" << name
                           << "' is not UPPER_CASE";
                }
            }
        }
    }
    SUCCEED();
}

TEST(LexerTokens, AllFlexLexemesHaveNames) {
    for (auto k : ParserToken::all()) {
        if (ParserToken::is_flex_lexeme(k)) {
            EXPECT_NE(ParserToken::name(k), "<unknown>")
                << "FLEX_LEXEME token has no name";
        }
    }
}

TEST(LexerTokens, NoFlexLexemeHasAstFlag) {
    for (auto k : ParserToken::all()) {
        if (ParserToken::is_flex_lexeme(k)) {
            EXPECT_FALSE(ParserToken::is_expr(k))
                << "FLEX_LEXEME " << ParserToken::name(k) << " has Expr flag";
            EXPECT_FALSE(ParserToken::is_stmt(k))
                << "FLEX_LEXEME " << ParserToken::name(k) << " has Stmt flag";
            EXPECT_FALSE(ParserToken::is_decl(k))
                << "FLEX_LEXEME " << ParserToken::name(k) << " has Decl flag";
            EXPECT_FALSE(ParserToken::is_root(k))
                << "FLEX_LEXEME " << ParserToken::name(k) << " has Root flag";
        }
    }
}

// Single-character tokens: verify they match ASCII
TEST(LexerTokens, SingleCharTokens) {
    std::set<std::pair<char, const char*>> expected = {
        {'(', "LPAREN"},   {')', "RPAREN"},
        {'{', "LBRACE"},   {'}', "RBRACE"},
        {'[', "LBRACKET"}, {']', "RBRACKET"},
        {'<', "LANGLE"},   {'>', "RANGLE"},
        {';', "SEMICOLON"},{',', "COMMA"},
        {':', "COLON"},    {'.', "DOT"},
        {'=', "ASSIGN"},   {'+', "PLUS"},
        {'-', "MINUS"},    {'*', "STAR"},
        {'/', "SLASH"},    {'%', "PERCENT"},
        {'&', "AMPERSAND"},{'|', "PIPE"},
        {'^', "CARET"},    {'~', "TILDE"},
        {'!', "BANG"},     {'?', "QUESTION"},
        {'@', "AT"},       {'$', "DOLLAR"},
        {'#', "HASH"},
    };

    for (const auto& [ch, name] : expected) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing single-char token: " << name;
        EXPECT_EQ(ParserToken::name_by_value(static_cast<int>(*k)), name);
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k))
            << "Single-char token " << name << " must be FLEX_LEXEME";
    }
}

// EOF token
TEST(LexerTokens, EofToken) {
    auto* k = ParserToken::from_name("END");
    ASSERT_NE(k, nullptr);
    EXPECT_EQ(static_cast<int>(*k), 0);
    EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
    EXPECT_TRUE(ParserToken::is_bison_token(*k));
}

// String literal tokens
TEST(LexerTokens, StringLiteralTokens) {
    const char* names[] = {"STRWIDE", "STRCHAR", "STRWIDE_RAW", "STRCHAR_RAW"};
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k))
            << name << " must be FLEX_LEXEME";
    }
}

// Number tokens
TEST(LexerTokens, NumberTokens) {
    const char* names[] = {"NUMBER", "INTEGER", "COMPLEX", "RATIONAL"};
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k))
            << name << " must be FLEX_LEXEME";
        EXPECT_TRUE(ParserToken::is_bison_token(*k))
            << name << " must be BISON_TOKEN";
    }
}

// Name tokens
TEST(LexerTokens, NameTokens) {
    const char* names[] = {"NAME", "MANGLED", "LOCAL", "NATIVE", "MODULE", "MACRO"};
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
        EXPECT_TRUE(ParserToken::is_bison_token(*k));
    }
}

// Macro tokens
TEST(LexerTokens, MacroTokens) {
    const char* names[] = {
        "MACRO_ARGNAME", "MACRO_ARGPOS", "MACRO_ARGUMENT", "MACRO_ARGCOUNT",
        "MACRO_CONCAT", "MACRO_TOSTR", "MACRO_DEL", "MACRO_SEQ", "MACRO_STR",
        "MACRO_EXPR_BEGIN", "MACRO_EXPR_END", "MACRO_NAMESPACE",
    };
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k))
            << name << " must be FLEX_LEXEME";
    }
}

// Operator tokens
TEST(LexerTokens, OperatorTokens) {
    const char* names[] = {
        "OPERATOR_DIV", "OP_MATH", "OP_COMPARE", "OP_LOGICAL",
        "OPERATOR_AND", "OPERATOR_PTR", "OPERATOR_DUCK",
        "OPERATOR_ANGLE_EQ", "OP_BITWISE",
    };
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
    }
}

// Integer operation tokens
TEST(LexerTokens, IntOpTokens) {
    const char* names[] = {"INT_PLUS", "INT_MINUS", "INT_REPEAT"};
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
    }
}

// Try block tokens
TEST(LexerTokens, TryBlockTokens) {
    const char* names[] = {
        "TRY_ALL_BEGIN", "TRY_ALL_END", "TRY_PLUS_BEGIN", "TRY_PLUS_END",
        "TRY_MINUS_BEGIN", "TRY_MINUS_END",
    };
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
    }
}

// Async/Interrupt tokens
TEST(LexerTokens, InterruptTokens) {
    const char* names[] = {
        "AWAIT", "YIELD", "YIELD_BEGIN", "YIELD_END", "WHEN_ALL", "WHEN_ANY",
    };
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
    }
}

// Iterator tokens
TEST(LexerTokens, IteratorTokens) {
    const char* names[] = {"ITERATOR", "ITERATOR_QQ"};
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
    }
}

// Reference tokens
TEST(LexerTokens, ReferenceTokens) {
    const char* names[] = {"TAKE_CONST", "WITH", "ESCAPE"};
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
    }
}

// Doc tokens
TEST(LexerTokens, DocTokens) {
    const char* names[] = {"DOC_BEFORE", "DOC_AFTER"};
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
    }
}

// Control tokens
TEST(LexerTokens, ControlTokens) {
    const char* names[] = {
        "ELLIPSIS", "RANGE", "REPEAT", "FOLLOW", "MATCHING",
        "CREATE_NEW", "CREATE_USE", "APPEND",
        "PURE_NEW", "PURE_USE", "SWAP",
        "ATTRIBUTE", "PARENT", "NEWLANG",
        "NAMESPACE", "ARGUMENT", "ARGS",
    };
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
        EXPECT_TRUE(ParserToken::is_bison_token(*k));
    }
}

// Embed/Eval tokens
TEST(LexerTokens, EmbedEvalTokens) {
    const char* names[] = {"EMBED", "EVAL"};
    for (const char* name : names) {
        auto* k = ParserToken::from_name(name);
        ASSERT_NE(k, nullptr) << "Missing: " << name;
        EXPECT_TRUE(ParserToken::is_flex_lexeme(*k));
    }
}

// Count flex lexemes — exact count to catch accidental additions/removals
TEST(LexerTokens, FlexLexemeCount) {
    int count = 0;
    for (auto k : ParserToken::all()) {
        if (ParserToken::is_flex_lexeme(k)) ++count;
    }
    EXPECT_EQ(count, 104) << "Expected exact count of FLEX_LEXEME tokens";
}

// No duplicates in all token names
TEST(LexerTokens, AllNamesUnique) {
    std::set<std::string> seen;
    for (auto k : ParserToken::all()) {
        std::string name(ParserToken::name(k));
        EXPECT_NE(seen.count(name), 1u) << "Duplicate name: " << name;
        seen.insert(name);
    }
}

} // namespace trust