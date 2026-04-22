#include "parser/mmproc.hpp"
#include "diag/context.hpp"
#include "parser/lexer.hpp"
#include <gtest/gtest.h>
#include <string>

namespace trust {

// Helper: run MMProc and capture error count
static int RunMMProcErrorCount(const std::string &input) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", input);
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto ast = MMProcessor::process(ctx, lexemes);
    (void)ast;
    return ctx.diag().errorCount();
}

// Test: empty input produces empty AST
TEST(MMProcTest, EmptyInput) {
    int errors = RunMMProcErrorCount("");
    EXPECT_EQ(errors, 0);
}

// Test: NAME token produces an Ident AST node
TEST(MMProcTest, NameTokenProducesIdentNode) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: single string literal produces a StringLiteral node
TEST(MMProcTest, SingleStringLiteral) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "\"hello\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    auto *str = ast[0]->as<StringLiteral>();
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->value(), "hello");
    ASSERT_TRUE(str->source.has_value());
    EXPECT_EQ(str->source->kind, ParserToken::Kind::STRWIDE);
}

// Test: consecutive strings of same type are concatenated
TEST(MMProcTest, StringConcatenationWide) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "\"hello\" \"world\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    auto *str = ast[0]->as<StringLiteral>();
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->value(), "helloworld");
}

// Test: consecutive char strings are concatenated
TEST(MMProcTest, StringConcatenationChar) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "'a' 'b' 'c'");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    auto *str = ast[0]->as<StringLiteral>();
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->value(), "abc");
}

// Test: strings of different types are NOT concatenated
TEST(MMProcTest, DifferentStringTypesNoConcat) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "\"wide\" r\"raw\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 2);
    auto *s1 = ast[0]->as<StringLiteral>();
    auto *s2 = ast[1]->as<StringLiteral>();
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s1->value(), "wide");
    EXPECT_EQ(s2->value(), "raw");
}

// Test: MACRO token generates error
TEST(MMProcTest, MacroTokenError) {
    int errors = RunMMProcErrorCount("@foo := bar;");
    EXPECT_GE(errors, 1);
}

// Test: MODULE token generates error
TEST(MMProcTest, ModuleTokenError) {
    int errors = RunMMProcErrorCount("\\foo()");
    EXPECT_GE(errors, 1);
}

// Test: TokenInfo range is set for concatenated strings
TEST(MMProcTest, TokenInfoRangeConcatenated) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "\"a\" \"b\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "ab");
}

// Test: raw string concatenation
TEST(MMProcTest, RawStringConcatenation) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "r\"hello\" r\"world\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    auto *str = ast[0]->as<StringLiteral>();
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->value(), "helloworld");
    ASSERT_TRUE(str->source.has_value());
    EXPECT_EQ(str->source->kind, ParserToken::Kind::STRWIDE_RAW);
}

// ========== Identifier merge tests ==========

// Test: single NAME → Ident (1 lexeme → 1 Ident)
TEST(MMProcTest, IdentFromName) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: NAME
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "foo");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAMESPACE + NAME → Ident (2 lexemes → 1 Ident)
TEST(MMProcTest, IdentFromNamespaceName) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "::foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: NAMESPACE, NAME
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "::foo");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAME NAMESPACE NAME → Ident (3 lexemes → 1 Ident)
TEST(MMProcTest, IdentFromNameNamespaceName) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "foo::bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 3u); // 3 fragments: NAME, NAMESPACE, NAME
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "foo::bar");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: full path with namespaces (6 lexemes → 1 Ident)
TEST(MMProcTest, IdentFromFullPath) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "::foo::bar::baz");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 6u); // 6 fragments: NAMESPACE, NAME, NAMESPACE, NAME, NAMESPACE, NAME
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "::foo::bar::baz");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAME + LOCAL → Ident (2 lexemes → 1 Ident)
TEST(MMProcTest, IdentFromNameLocal) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "foo$local");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: NAME, LOCAL
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "foo$local");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAMESPACE + NAME + LOCAL → Ident (3 lexemes → 1 Ident)
TEST(MMProcTest, IdentFromNamespaceNameLocal) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "::foo$local");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 3u); // 3 fragments: NAMESPACE, NAME, LOCAL
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "::foo$local");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAME + NAMESPACE + NAME + LOCAL → Ident (4 lexemes → 1 Ident)
TEST(MMProcTest, IdentFromPathWithLocal) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "bar::foo$local");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 4u); // 4 fragments: NAME, NAMESPACE, NAME, LOCAL
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "bar::foo$local");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: MANGLED → Ident (1 lexeme → 1 Ident)
TEST(MMProcTest, IdentFromMangled) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "_$foo$_bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: MANGLED
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "_$foo$_bar");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAME NATIVE → Ident (2 lexemes → 1 Ident)
TEST(MMProcTest, IdentFromNameNative) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "foo%native");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: NAME, NATIVE
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "foo%native");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: два NAME подряд без NAMESPACE — не сливаются (2 lexemes → 2 Ident)
TEST(MMProcTest, TwoNamesNotMerged) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "foo bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: NAME, NAME
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 2);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    EXPECT_EQ(ast[1]->token_kind(), ParserToken::Kind::Ident);
    EXPECT_EQ(ast[0]->source->text, "foo");
    EXPECT_EQ(ast[1]->source->text, "bar");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: одиночный NATIVE → Ident (1 lexeme → 1 Ident)
TEST(MMProcTest, IdentFromNativeStandalone) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "%native");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: NATIVE
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::Ident);
    auto &ti = ast[0]->source;
    ASSERT_TRUE(ti.has_value());
    EXPECT_EQ(ti->text, "%native");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAMESPACE без имени — остаётся как TokenInfo (1 lexeme → 1 NAMESPACE node)
TEST(MMProcTest, NamespaceOnlySkipped) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "::");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: NAMESPACE
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1u); // NAMESPACE без имени — остаётся как узел
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::NAMESPACE);
    ASSERT_TRUE(ast[0]->source.has_value());
    EXPECT_EQ(ast[0]->source->text, "::");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// ========== EMBED tests ==========

// Test: EMBED token produces EmbedExpr node
TEST(MMProcTest, EmbedCreatesEmbedExpr) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "{% code %}");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: EMBED
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1u);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::EmbedExpr);
    auto *embed = ast[0]->as<EmbedExpr>();
    ASSERT_NE(embed, nullptr);
    EXPECT_EQ(embed->value(), " code ");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: multiple consecutive EMBED tokens are concatenated
TEST(MMProcTest, EmbedConcatenation) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "{% a %} {% b %}");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: EMBED, EMBED
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 1u);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::EmbedExpr);
    auto *embed = ast[0]->as<EmbedExpr>();
    ASSERT_NE(embed, nullptr);
    EXPECT_EQ(embed->value(), " a  b ");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: EMBED with other tokens
TEST(MMProcTest, EmbedWithIdent) {
    Context ctx;
    SourceIdx idx = ctx.add_source("<test>", "{% code %} foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // EMBED, NAME
    auto ast = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ast.size(), 2u);
    EXPECT_EQ(ast[0]->token_kind(), ParserToken::Kind::EmbedExpr);
    EXPECT_EQ(ast[1]->token_kind(), ParserToken::Kind::Ident);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

} // namespace trust
