#include "parser/mmproc.hpp"
#include "diag/context.hpp"
#include "parser/lexer.hpp"
#include <gtest/gtest.h>
#include <string>

namespace trust {

// Helper: run MMProc and capture error count
static int RunMMProcErrorCount(const std::string &input) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", input);
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    (void)tokens;
    return ctx.diag().errorCount();
}

// Test: empty input produces empty token sequence
TEST(MMProcTest, EmptyInput) {
    int errors = RunMMProcErrorCount("");
    EXPECT_EQ(errors, 0);
}

// Test: NAME token produces an Ident token
TEST(MMProcTest, NameTokenProducesIdentToken) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: single string literal produces a STRWIDE token
TEST(MMProcTest, SingleStringLiteral) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "\"hello\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::STRWIDE);
    EXPECT_EQ(tokens[0]->text, "hello");
}

// Test: consecutive strings of same type are concatenated
TEST(MMProcTest, StringConcatenationWide) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "\"hello\" \"world\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::STRWIDE);
    EXPECT_EQ(tokens[0]->text, "helloworld");
}

// Test: consecutive char strings are concatenated
TEST(MMProcTest, StringConcatenationChar) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "'a' 'b' 'c'");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::STRCHAR);
    EXPECT_EQ(tokens[0]->text, "abc");
}

// Test: strings of different types are NOT concatenated
TEST(MMProcTest, DifferentStringTypesNoConcat) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "\"wide\" r\"raw\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::STRWIDE);
    EXPECT_EQ(tokens[0]->text, "wide");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::STRWIDE_RAW);
    EXPECT_EQ(tokens[1]->text, "raw");
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

// Test: TokenInfo text is set for concatenated strings
TEST(MMProcTest, TokenInfoTextConcatenated) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "\"a\" \"b\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->text, "ab");
}

// Test: raw string concatenation (preserves text without unescape)
TEST(MMProcTest, RawStringConcatenation) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "r\"hello\" r\"world\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::STRWIDE_RAW);
    EXPECT_EQ(tokens[0]->text, "helloworld");
}

// ========== Identifier merge tests ==========

// Test: single NAME → Ident token
TEST(MMProcTest, IdentFromName) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: NAME
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo");
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAMESPACE + NAME → Ident token
TEST(MMProcTest, IdentFromNamespaceName) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "::foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: NAMESPACE, NAME
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "::foo");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAME NAMESPACE NAME → Ident token
TEST(MMProcTest, IdentFromNameNamespaceName) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "foo::bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 3u); // 3 fragments: NAME, NAMESPACE, NAME
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo::bar");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: full path with namespaces (6 lexemes → 1 Ident)
TEST(MMProcTest, IdentFromFullPath) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "::foo::bar::baz");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 6u); // 6 fragments: NAMESPACE, NAME, NAMESPACE, NAME, NAMESPACE, NAME
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "::foo::bar::baz");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAME + LOCAL → Ident token
TEST(MMProcTest, IdentFromNameLocal) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "foo$local");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: NAME, LOCAL
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo$local");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAMESPACE + NAME + LOCAL → Ident token
TEST(MMProcTest, IdentFromNamespaceNameLocal) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "::foo$local");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 3u); // 3 fragments: NAMESPACE, NAME, LOCAL
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "::foo$local");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAME + NAMESPACE + NAME + LOCAL → Ident token
TEST(MMProcTest, IdentFromPathWithLocal) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "bar::foo$local");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 4u); // 4 fragments: NAME, NAMESPACE, NAME, LOCAL
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "bar::foo$local");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: MANGLED → Ident token
TEST(MMProcTest, IdentFromMangled) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "_$foo$_bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: MANGLED
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "_$foo$_bar");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAME NATIVE → Ident token
TEST(MMProcTest, IdentFromNameNative) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "foo%native");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: NAME, NATIVE
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo%native");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: два NAME подряд без NAMESPACE — не сливаются (2 tokens)
TEST(MMProcTest, TwoNamesNotMerged) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "foo bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: NAME, NAME
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo");
    EXPECT_EQ(tokens[1]->text, "bar");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: одиночный NATIVE → Ident token
TEST(MMProcTest, IdentFromNativeStandalone) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "%native");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: NATIVE
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "%native");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: NAMESPACE без имени — остаётся как NAMESPACE token
TEST(MMProcTest, NamespaceOnlySkipped) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "::");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: NAMESPACE
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1u); // NAMESPACE без имени — остаётся как токен
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::NAMESPACE);
    EXPECT_EQ(tokens[0]->text, "::");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// ========== EMBED tests ==========

// Test: EMBED token produces EMBED token (no conversion, just TokenInfo)
TEST(MMProcTest, EmbedStaysEmbed) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "{% code %}");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 1u); // 1 fragment: EMBED
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::EMBED);
    EXPECT_EQ(tokens[0]->text, " code ");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: multiple consecutive EMBED tokens are concatenated
TEST(MMProcTest, EmbedConcatenation) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "{% a %} {% b %}");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // 2 fragments: EMBED, EMBED
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::EMBED);
    EXPECT_EQ(tokens[0]->text, " a  b ");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

// Test: EMBED with other tokens
TEST(MMProcTest, EmbedWithIdent) {
    Context ctx;
    FileIdx idx = ctx.add_source("<test>", "{% code %} foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    ASSERT_EQ(lexemes.size(), 2u); // EMBED, NAME
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::EMBED);
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

} // namespace trust