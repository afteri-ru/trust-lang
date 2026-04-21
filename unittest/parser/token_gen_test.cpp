// token_gen_test.cpp - validates token generation correctness using GTest.
#include "parser/token.hpp"
#include <gtest/gtest.h>
#include <string_view>
#include <cstdint>

namespace trust {

// Enum traits compile-time checks
TEST(TokenGen, EnumTraits) {
    static_assert(std::is_scoped_enum_v<ParserToken::Kind>, "ParserToken::Kind must be scoped enum");
    static_assert(std::is_scoped_enum_v<TokenFlag>, "TokenFlag must be scoped enum");
    static_assert(std::is_scoped_enum_v<TokenCategory>, "TokenCategory must be scoped enum");
    static_assert(std::is_same_v<std::underlying_type_t<ParserToken::Kind>, int>,
                  "ParserToken::Kind underlying = int");
    static_assert((TokenFlag::FLEX_LEXEME | TokenFlag::BISON_TOKEN) != TokenFlag{}, "TokenFlag | operator works");
    static_assert((TokenFlag::FLEX_LEXEME & TokenFlag::BISON_TOKEN) == TokenFlag{}, "TokenFlag & operator works");
    SUCCEED();
}

TEST(TokenGen, TokenName) {
    [[maybe_unused]] std::string_view n1 = ParserToken::name(ParserToken::Kind::END);
    [[maybe_unused]] std::string_view n2 = ParserToken::name(ParserToken::Kind::IntLiteral);
    [[maybe_unused]] std::string_view n3 = ParserToken::name(ParserToken::Kind::IfStmt);
    [[maybe_unused]] std::string_view n4 = ParserToken::name(ParserToken::Kind::FuncDecl);
    [[maybe_unused]] std::string_view n5 = ParserToken::name(ParserToken::Kind::Program);
    SUCCEED();
}

TEST(TokenGen, NameByValue) {
    EXPECT_EQ(ParserToken::name_by_value(0), "END");
    EXPECT_EQ(ParserToken::name_by_value(-1), "<unknown>");

    auto* expr_k = ParserToken::from_name("IntLiteral");
    if (expr_k) {
        EXPECT_EQ(ParserToken::name_by_value(static_cast<int>(*expr_k)), "IntLiteral");
    }
}

TEST(TokenGen, CoreInvariants) {
    EXPECT_EQ(static_cast<int>(ParserToken::Kind::END), 0);
}

TEST(TokenGen, NamesNotUnknown) {
    for (auto k : ParserToken::all()) {
        EXPECT_NE(ParserToken::name(k), "<unknown>")
            << "ParserToken::Kind " << static_cast<int>(k) << " has name '<unknown>'";
    }
}

TEST(TokenGen, NoDuplicates) {
    const auto& all = ParserToken::all();
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            EXPECT_NE(static_cast<int>(all[i]), static_cast<int>(all[j]))
                << "Duplicate value for " << ParserToken::name(all[i]) << " and " << ParserToken::name(all[j]);
        }
    }
}

TEST(TokenGen, AstCounts) {
    std::size_t expr_count = 0, stmt_count = 0, decl_count = 0, root_count = 0;

    for (auto k : ParserToken::all()) {
        if (ParserToken::is_expr(k)) ++expr_count;
        if (ParserToken::is_stmt(k)) ++stmt_count;
        if (ParserToken::is_decl(k)) ++decl_count;
        if (ParserToken::is_root(k)) ++root_count;
    }

    EXPECT_EQ(root_count, 1) << "Exactly one Root node";
    EXPECT_GT(expr_count, 0) << "Has Expr nodes";
    EXPECT_GT(stmt_count, 0) << "Has Stmt nodes";
    EXPECT_GT(decl_count, 0) << "Has Decl nodes";
}

TEST(TokenGen, AutoRange) {
    int min_auto = INT32_MAX;
    for (auto k : ParserToken::all()) {
        int v = static_cast<int>(k);
        if (v > 0 && v < min_auto) min_auto = v;
    }
    EXPECT_GE(min_auto, 256) << "AUTO values >= 256";
}

TEST(TokenGen, RuntimeConsistency) {
    validateTokenGenConsistency();
    SUCCEED();
}

TEST(TokenGen, Predicates) {
    EXPECT_TRUE(ParserToken::is_flex_lexeme(ParserToken::Kind::END));
    EXPECT_TRUE(ParserToken::is_bison_token(ParserToken::Kind::END));
    EXPECT_FALSE(ParserToken::is_flex_lexeme(ParserToken::Kind::IntLiteral));
    EXPECT_TRUE(ParserToken::is_bison_token(ParserToken::Kind::IntLiteral));
    EXPECT_TRUE(ParserToken::is_expr(ParserToken::Kind::IntLiteral));
    EXPECT_TRUE(ParserToken::is_stmt(ParserToken::Kind::IfStmt));
    EXPECT_TRUE(ParserToken::is_decl(ParserToken::Kind::FuncDecl));
    EXPECT_TRUE(ParserToken::is_root(ParserToken::Kind::Program));
    EXPECT_FALSE(ParserToken::is_expr(ParserToken::Kind::NAME));
}

TEST(TokenGen, AstViaPredicates) {
    EXPECT_TRUE(ParserToken::is_expr(ParserToken::Kind::IntLiteral));
    EXPECT_TRUE(ParserToken::is_stmt(ParserToken::Kind::IfStmt));
    EXPECT_TRUE(ParserToken::is_decl(ParserToken::Kind::FuncDecl));
    EXPECT_TRUE(ParserToken::is_root(ParserToken::Kind::Program));
    EXPECT_FALSE(ParserToken::is_expr(ParserToken::Kind::NAME));
    EXPECT_FALSE(ParserToken::is_stmt(ParserToken::Kind::NAME));
    EXPECT_FALSE(ParserToken::is_decl(ParserToken::Kind::NAME));
    EXPECT_FALSE(ParserToken::is_root(ParserToken::Kind::NAME));
}

TEST(TokenGen, Lookup) {
    auto* k1 = ParserToken::from_name("END");
    ASSERT_NE(k1, nullptr);
    EXPECT_EQ(*k1, ParserToken::Kind::END);

    EXPECT_EQ(ParserToken::from_name("NONEXISTENT"), nullptr);

    auto* k3 = ParserToken::from_value(0);
    ASSERT_NE(k3, nullptr);
    EXPECT_EQ(*k3, ParserToken::Kind::END);

    EXPECT_EQ(ParserToken::from_value(-1), nullptr);
}

TEST(TokenGen, FlagsType) {
    TokenFlag f = ParserToken::flags(ParserToken::Kind::END);
    EXPECT_EQ(f, TokenFlag::FLEX_LEXEME | TokenFlag::BISON_TOKEN);
    EXPECT_TRUE(ParserToken::is_flex_lexeme(ParserToken::Kind::END));

    EXPECT_TRUE((ParserToken::flags(ParserToken::Kind::IntLiteral) & TokenFlag::Expr) != TokenFlag{});
    EXPECT_TRUE((ParserToken::flags(ParserToken::Kind::NAME) & TokenFlag::FLEX_LEXEME) != TokenFlag{});
    EXPECT_TRUE((ParserToken::flags(ParserToken::Kind::NAME) & TokenFlag::BISON_TOKEN) != TokenFlag{});
}

TEST(TokenGen, AstNoOverlap) {
    for (auto k : ParserToken::all()) {
        int flags = static_cast<int>(ParserToken::flags(k));
        int ast_count = 0;
        if (flags & static_cast<int>(TokenFlag::Expr)) ++ast_count;
        if (flags & static_cast<int>(TokenFlag::Stmt)) ++ast_count;
        if (flags & static_cast<int>(TokenFlag::Decl)) ++ast_count;
        if (flags & static_cast<int>(TokenFlag::Root)) ++ast_count;
        EXPECT_LE(ast_count, 1) << ParserToken::name(k).data() << " has " << ast_count << " AST flags";
    }
}

TEST(TokenGen, TokenCategory) {
    EXPECT_EQ(static_cast<int>(TokenCategory::None), 0);
    EXPECT_EQ(static_cast<int>(TokenCategory::Expr), 1);
    EXPECT_EQ(static_cast<int>(TokenCategory::Stmt), 2);
    EXPECT_EQ(static_cast<int>(TokenCategory::Decl), 3);
    EXPECT_EQ(static_cast<int>(TokenCategory::Root), 4);
}

} // namespace trust