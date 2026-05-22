#include "parser/mmproc.hpp"
#include "diag/context.hpp"
#include "parser/lexer.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace trust {

// ========== Основные тесты (конкатенация строк, Ident, MANGLED, EMBED) ==========

TEST(MMProcTest, EmptyInput) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    EXPECT_TRUE(tokens.empty());
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, NameTokenProducesIdentToken) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, SingleStringLiteral) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "\"hello\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::STRWIDE);
    EXPECT_EQ(tokens[0]->text, "hello");
}

TEST(MMProcTest, StringConcatenationWide) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "\"hello\" \"world\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::STRWIDE);
    EXPECT_EQ(tokens[0]->text, "helloworld");
}

TEST(MMProcTest, DifferentStringTypesNoConcat) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "\"wide\" r\"raw\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::STRWIDE);
    EXPECT_EQ(tokens[0]->text, "wide");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::STRWIDE_RAW);
    EXPECT_EQ(tokens[1]->text, "raw");
}

TEST(MMProcTest, ModuleTokenError) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "\\foo()");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    EXPECT_GE(ctx.diag().errorCount(), 1);
}

TEST(MMProcTest, RawStringConcatenation) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "r\"hello\" r\"world\"");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::STRWIDE_RAW);
    EXPECT_EQ(tokens[0]->text, "helloworld");
}

// ========== Тесты идентификаторов ==========

TEST(MMProcTest, IdentFromName) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo");
}

TEST(MMProcTest, IdentFromNamespaceName) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "::foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "::foo");
}

TEST(MMProcTest, IdentFromNameNamespaceName) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "foo::bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo::bar");
}

TEST(MMProcTest, IdentFromFullPath) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "::foo::bar::baz");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "::foo::bar::baz");
}

TEST(MMProcTest, IdentFromNameLocal) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "foo$local");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo$local");
}

TEST(MMProcTest, IdentFromMangled) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "_$foo$_bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "_$foo$_bar");
}

TEST(MMProcTest, IdentFromNameNative) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "foo%native");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo%native");
}

TEST(MMProcTest, TwoNamesNotMerged) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "foo bar");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo");
    EXPECT_EQ(tokens[1]->text, "bar");
}

TEST(MMProcTest, NamespaceOnlySkipped) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "::");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::NAMESPACE);
    EXPECT_EQ(tokens[0]->text, "::");
}

// ========== Тесты EMBED ==========

TEST(MMProcTest, EmbedStaysEmbed) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "{% code %}");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::EMBED);
    EXPECT_EQ(tokens[0]->text, " code ");
}

// ========== Тесты определений макросов ==========

TEST(MMProcTest, MacroDefSimple) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ my_macro @@ ::= term;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& reg = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(reg.size(), 1u);

    auto it = reg.find("my_macro");
    ASSERT_NE(it, reg.end());
    const auto& def = it->second;
    EXPECT_EQ(def.m_name, "my_macro");
    EXPECT_EQ(def.m_bodyType, MacroBodyType::kExpression);
    ASSERT_EQ(def.m_body.size(), 1u);
    EXPECT_EQ(def.m_body[0]->text, "term");
    EXPECT_TRUE(def.m_params.empty());
}

TEST(MMProcTest, MacroDefWithParams) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ FUNC $name $value @@ ::= @$name @$value;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& reg = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(reg.size(), 1u);

    auto it = reg.find("FUNC");
    ASSERT_NE(it, reg.end());
    const auto& def = it->second;
    EXPECT_EQ(def.m_name, "FUNC");
    ASSERT_EQ(def.m_params.size(), 2u);
    EXPECT_EQ(def.m_params[0], "name");
    EXPECT_EQ(def.m_params[1], "value");
}

TEST(MMProcTest, MacroDefTokenSeqBody) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ macro @@ ::= @@ do_something(42) ; @@;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& reg = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(reg.size(), 1u);
    auto it = reg.find("macro");
    ASSERT_NE(it, reg.end());
    EXPECT_EQ(it->second.m_bodyType, MacroBodyType::kTokenSequence);
    ASSERT_GE(it->second.m_body.size(), 3u);
}

TEST(MMProcTest, MacroDefStringBody) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ macro @@ ::= @@@ text body @@@;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& reg = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(reg.size(), 1u);
    auto it = reg.find("macro");
    ASSERT_NE(it, reg.end());
    EXPECT_EQ(it->second.m_bodyType, MacroBodyType::kStringLiteral);
    ASSERT_EQ(it->second.m_body.size(), 1u);
    EXPECT_EQ(it->second.m_body[0]->text, " text body ");
}

TEST(MMProcTest, MacroRedefined) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ macro @@ ::= a; @@ macro @@ ::= b;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);
    EXPECT_GE(ctx.diag().errorCount(), 1);
    EXPECT_EQ(MMProcessor::currentMacros(*macros).size(), 1u);
}

// ========== Тесты раскрытия макросов ==========

TEST(MMProcTest, MacroExpandSimple) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ macro @@ := term; @macro;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(MMProcessor::currentMacros(*macros).size(), 1u);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "term");
}

TEST(MMProcTest, MacroExpandWithArg) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ show $x @@ := @$x; @show 42;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->text, "42");
}

TEST(MMProcTest, MacroExpandWithPosArg) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@@ pick $a $b @@ := @$2; @pick x y;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->text, "y");
}

// Test: вызов неопределённого макроса → ошибка
TEST(MMProcTest, MacroUndefined) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@undefined;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes);
    EXPECT_GE(ctx.diag().errorCount(), 1);
}

// Test: MACRO_STR (@@@...@@@) → StringLiteral
TEST(MMProcTest, MacroStrToLiteral) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@@@ text @@@");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    EXPECT_EQ(tokens[0]->text, " text ");
}

// Test: макрос с аргументами в скобках
TEST(MMProcTest, MacroWithParenArgs) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ twice ( $x ) @@ := @$x @$x; @twice(42);");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& reg = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(reg.size(), 1u);

    auto it = reg.find("twice");
    ASSERT_NE(it, reg.end());
    EXPECT_EQ(it->second.m_params.size(), 1u);
    EXPECT_EQ(it->second.m_params[0], "x");

    // Раскрытие: @twice(42) → 42 42
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0]->text, "42");
    EXPECT_EQ(tokens[1]->text, "42");
}

// Test: макрос с многословным именем (несколько NAME токенов)
// Макросы идентифицируются по первой лексеме имени
TEST(MMProcTest, MacroMultiwordName) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ MY MACRO @@ := result; @MY MACRO;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& reg = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(reg.size(), 1u);

    // Ключ — первая лексема "MY"
    auto it = reg.find("MY");
    ASSERT_NE(it, reg.end());
    EXPECT_EQ(it->second.m_name, "MY MACRO"); // полное имя сохраняется в m_name

    // Раскрытие: @MY MACRO → "result"
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->text, "result");
}

// Test: макрос с пустым телом в @@ ... @@
TEST(MMProcTest, MacroDefTokenSeqEmptyBody) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ empty @@ ::= @@ @@;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& reg = MMProcessor::currentMacros(*macros);
    auto it = reg.find("empty");
    ASSERT_NE(it, reg.end());
    EXPECT_EQ(it->second.m_bodyType, MacroBodyType::kTokenSequence);
    EXPECT_TRUE(it->second.m_body.empty());
}

// ========== Тесты аргументов в скобках с запятыми ==========

// Test: определение макроса с несколькими аргументами в скобках и запятой
TEST(MMProcTest, MacroDefParenArgsWithComma) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ f ( $x, $y ) @@ := @$x @$y; @f(a, b);");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& reg = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(reg.size(), 1u);

    auto it = reg.find("f");
    ASSERT_NE(it, reg.end());
    ASSERT_EQ(it->second.m_params.size(), 2u);
    EXPECT_EQ(it->second.m_params[0], "x");
    EXPECT_EQ(it->second.m_params[1], "y");

    // Раскрытие: @f(a, b) → a b
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0]->text, "a");
    EXPECT_EQ(tokens[1]->text, "b");
}

// Test: макрос с одним аргументом в скобках, содержащим вложенные скобки и запятые
TEST(MMProcTest, MacroCallNestedParensInsideArg) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ g ( $x, $y ) @@ := @$1 @$2; @g(f(1, 2), g(3, 4));");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    // После раскрытия: каждый токен аргументов вставляется отдельно
    // f ( 1 , 2 ) g ( 3 , 4 ) — 12 токенов
    ASSERT_EQ(tokens.size(), 12u);
    EXPECT_EQ(tokens[0]->text, "f");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::LPAREN);
    EXPECT_EQ(tokens[2]->text, "1");
    EXPECT_EQ(tokens[3]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[4]->text, "2");
    EXPECT_EQ(tokens[5]->kind, ParserToken::Kind::RPAREN);
    EXPECT_EQ(tokens[6]->text, "g");
    EXPECT_EQ(tokens[7]->kind, ParserToken::Kind::LPAREN);
    EXPECT_EQ(tokens[8]->text, "3");
    EXPECT_EQ(tokens[9]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[10]->text, "4");
    EXPECT_EQ(tokens[11]->kind, ParserToken::Kind::RPAREN);
}

// Test: пустой список аргументов
TEST(MMProcTest, MacroCallParensEmpty) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ f ( ) @@ := 42; @f();");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->text, "42");
}

// ========== Тесты строкового тела макроса (третий тип) ==========

// Test: строковое тело с подстановкой аргумента (число)
TEST(MMProcTest, MacroStringBodySubst) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ v ( $x ) @@ := @@@ @$x @@@; @v(42);");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->text, "42");
}

// Test: строковое тело с подстановкой нескольких аргументов
TEST(MMProcTest, MacroStringBodyMultiple) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ test ( $x, $y ) @@ := @@@ get(@$x, @$y) @@@; @test(a, b);");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    // Ожидаем: Ident "get", LPAREN, Ident "a", COMMA, Ident "b", RPAREN
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "get");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::LPAREN);
    EXPECT_EQ(tokens[2]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[2]->text, "a");
    EXPECT_EQ(tokens[3]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[4]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[4]->text, "b");
    EXPECT_EQ(tokens[5]->kind, ParserToken::Kind::RPAREN);
}

// ========== Тесты @$* и @$# с аргументами в скобках ==========

// Test: @$* с несколькими аргументами в скобках
TEST(MMProcTest, MacroArgStarWithParens) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ join ( $x, $y ) @@ := [@$*]; @join(a, b);");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::LBRACKET);
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[1]->text, "a, b");
    EXPECT_EQ(tokens[2]->kind, ParserToken::Kind::RBRACKET);
}

// Test: @$# с несколькими аргументами в скобках
TEST(MMProcTest, MacroArgHashWithParens) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ cnt ( $x, $y ) @@ := @$#; @cnt(a, b);");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::IntLiteral);
    EXPECT_EQ(tokens[0]->text, "2");
}

// ========== Тесты диагностики ошибок ==========

// Test: определение макроса без ';'
TEST(MMProcTest, MacroDefNoSemicolon) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ m @@ := body");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);
    EXPECT_GE(ctx.diag().errorCount(), 1);
}

// Test: незакрытое имя макроса (отсутствует @@ после имени)
TEST(MMProcTest, MacroDefUnterminatedName) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ m");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);
    EXPECT_GE(ctx.diag().errorCount(), 1);
}

// Test: незакрытое тело макроса (отсутствует @@ после тела в TokenSequence)
TEST(MMProcTest, MacroDefUnterminatedBody) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ m @@ ::= @@ body");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);
    EXPECT_GE(ctx.diag().errorCount(), 1);
}

// Test: пустое имя макроса
TEST(MMProcTest, MacroDefEmptyName) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ @@ ::= 42;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);
    EXPECT_GE(ctx.diag().errorCount(), 1);
}

// Test: маппинг при раскрытии макроса
TEST(MMProcTest, MacroExpandBodyMapping) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ my_macro @@ := term; @my_macro;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "term");

    // Проверяем, что маппинг создан: токен результата должен быть связан с определением макроса
    const auto* reader = ctx.toReader();
    ASSERT_NE(reader, nullptr);
    ReaderLocation loc = static_cast<ReaderLocation>(tokens[0]->range.begin);
    auto macro_range = reader->getMacroDefRange(loc);
    EXPECT_TRUE(macro_range.has_value());
}

// Test: вызов пустого макроса
TEST(MMProcTest, MacroEmptyCall) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ empty @@ ::= @@ @@; @empty;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    EXPECT_TRUE(tokens.empty());
}

// Test: foo:: — корректное разделение на Ident + NAMESPACE
TEST(MMProcTest, NamespaceTrailing) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "foo::");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);

    // foo:: → Ident("foo") + NAMESPACE("::")
    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "foo");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::NAMESPACE);
    EXPECT_EQ(tokens[1]->text, "::");
}

// ========== Тесты атрибутов @[...]@ ==========

TEST(MMProcTest, SimpleAttrGroup) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@[const]@ foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
    EXPECT_EQ(tokens[0]->m_sequence.size(), 1u);
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[1]->text, "foo");
}

TEST(MMProcTest, AttrGroupWithParam) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@[require(x > 0)]@ foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
    // Внутри ATTR: require, (, x, >, 0, )
    ASSERT_GE(tokens[0]->m_sequence.size(), 5u);
}

TEST(MMProcTest, MultipleAttrsBeforeToken) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@[const]@ @[readonly]@ foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::ATTR);
    EXPECT_EQ(tokens[2]->kind, ParserToken::Kind::Ident);
}

TEST(MMProcTest, UnclosedAttrError) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@[const foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    EXPECT_GE(ctx.diag().errorCount(), 1);
}

TEST(MMProcTest, UnexpectedAttrEndError) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "]@ foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    EXPECT_GE(ctx.diag().errorCount(), 1);
}

TEST(MMProcTest, AttrWithIntegerParam) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@[stack_guard(1024)]@ foo");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
    ASSERT_GE(tokens[0]->m_sequence.size(), 4u);
}

TEST(MMProcTest, AttrWithStringParam) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", R"(@[attr_name("hello")]@ foo)");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_EQ(ctx.diag().errorCount(), 0);

    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
}

} // namespace trust
