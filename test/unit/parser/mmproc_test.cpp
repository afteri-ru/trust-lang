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
    const auto& vec = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(vec.size(), 1u);

    const auto& def = vec[0];
    ASSERT_GE(def.m_nameLexemes.size(), 1u);
    EXPECT_EQ(std::string_view{def.m_nameLexemes[0]}, "my_macro");
    EXPECT_EQ(def.m_bodyType, MacroBodyType::kExpression);
    ASSERT_EQ(def.m_body.size(), 1u);
    EXPECT_EQ(std::string(def.m_body[0]), "term");
    EXPECT_TRUE(def.m_argGroups.empty());
}

TEST(MMProcTest, MacroDefWithParams) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ FUNC $name $value @@ ::= @$name @$value;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& vec = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(vec.size(), 1u);

    const auto& def = vec[0];
    ASSERT_GE(def.m_nameLexemes.size(), 1u);
    EXPECT_EQ(std::string_view{def.m_nameLexemes[0]}, "FUNC");
    ASSERT_EQ(def.m_argGroups.size(), 1u); // $name и $value — одна template-группа
    ASSERT_EQ(def.m_argGroups[0].m_params.size(), 2u);
    EXPECT_EQ(def.m_argGroups[0].m_params[0], "name");
    EXPECT_EQ(def.m_argGroups[0].m_params[1], "value");
}

TEST(MMProcTest, MacroDefTokenSeqBody) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ macro @@ ::= @@ do_something(42) ; @@;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& vec = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(vec.size(), 1u);
    EXPECT_EQ(vec[0].m_bodyType, MacroBodyType::kTokenSequence);
    ASSERT_GE(vec[0].m_body.size(), 3u);
}

TEST(MMProcTest, MacroDefStringBody) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ macro @@ ::= @@@ text body @@@;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& vec = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(vec.size(), 1u);
    EXPECT_EQ(vec[0].m_bodyType, MacroBodyType::kStringLiteral);
    ASSERT_EQ(vec[0].m_body.size(), 1u);
    EXPECT_EQ(std::string(vec[0].m_body[0]), " text body ");
}

TEST(MMProcTest, MacroRedefined) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ macro @@ ::= a; @@ macro @@ ::= b;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    MMProcessor::process(ctx, lexemes, macros);
    EXPECT_GE(ctx.diag().errorCount(), 1);
    // После ошибки только один макрос должен зарегистрироваться
    const auto& vec = MMProcessor::currentMacros(*macros);
    EXPECT_EQ(vec.size(), 1u);
}

// ========== Тесты раскрытия макросов ==========

TEST(MMProcTest, MacroExpandSimple) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ macro @@ := term; @macro;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    const auto& vec = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(vec.size(), 1u);
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
    const auto& vec = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(vec.size(), 1u);

    const auto& def = vec[0];
    ASSERT_GE(def.m_nameLexemes.size(), 1u);
    EXPECT_EQ(std::string_view{def.m_nameLexemes[0]}, "twice");
    ASSERT_EQ(def.m_argGroups.size(), 1u);
    ASSERT_EQ(def.m_argGroups[0].m_params.size(), 1u);
    EXPECT_EQ(def.m_argGroups[0].m_params[0], "x");

    // Раскрытие: @twice(42) → 42 42
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0]->text, "42");
    EXPECT_EQ(tokens[1]->text, "42");
}

// Test: макрос с многословным именем (несколько NAME токенов)
// Теперь макросы идентифицируются по полному набору лексем имени
TEST(MMProcTest, MacroMultiwordName) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ MY MACRO @@ := result; @MY MACRO;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    const auto& vec = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(vec.size(), 1u);

    const auto& def = vec[0];
    ASSERT_EQ(def.m_nameLexemes.size(), 2u);
    EXPECT_EQ(std::string_view{def.m_nameLexemes[0]}, "MY");
    EXPECT_EQ(std::string_view{def.m_nameLexemes[1]}, "MACRO");

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
    const auto& vec = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(vec.size(), 1u);
    EXPECT_EQ(vec[0].m_bodyType, MacroBodyType::kTokenSequence);
    EXPECT_TRUE(vec[0].m_body.empty());
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
    const auto& vec = MMProcessor::currentMacros(*macros);
    ASSERT_EQ(vec.size(), 1u);

    const auto& def = vec[0];
    ASSERT_EQ(def.m_argGroups.size(), 1u);
    ASSERT_EQ(def.m_argGroups[0].m_params.size(), 2u);
    EXPECT_EQ(def.m_argGroups[0].m_params[0], "x");
    EXPECT_EQ(def.m_argGroups[0].m_params[1], "y");

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
    // @$* с двумя аргументами → ( a , b , ) — отдельные токены
    // [@$*] → LBRACKET, LPAREN, Ident(a), COMMA, Ident(b), COMMA, RPAREN, RBRACKET
    ASSERT_EQ(tokens.size(), 8u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::LBRACKET);
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::LPAREN);
    EXPECT_EQ(tokens[2]->text, "a");
    EXPECT_EQ(tokens[3]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[4]->text, "b");
    EXPECT_EQ(tokens[5]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[6]->kind, ParserToken::Kind::RPAREN);
    EXPECT_EQ(tokens[7]->kind, ParserToken::Kind::RBRACKET);
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

// ========== Тесты раскрытия макросов внутри атрибутов + @depend_macro ==========

TEST(MMProcTest, MacroInsideAttrExpands) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ mymacro @@ := 42; @[const @mymacro]@ foo;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    ASSERT_EQ(ctx.diag().errorCount(), 0);
    // Ожидаем: ATTR(const, 42), ATTR(depend_macro("mymacro")), Ident(foo), SEMICOLON
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
    // Внутри первого ATTR: const и 42 (число раскрылось)
    ASSERT_EQ(tokens[0]->m_sequence.size(), 2u);
    EXPECT_EQ(tokens[0]->m_sequence[1]->text, "42");
    // Второй ATTR — depend_macro
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::ATTR);
    ASSERT_GE(tokens[1]->m_sequence.size(), 1u);
    EXPECT_EQ(tokens[1]->m_sequence[0]->text, "depend_macro");
    EXPECT_EQ(tokens[2]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[2]->text, "foo");
}

TEST(MMProcTest, DependMacroMultipleMacrosInAttr) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ m1 @@ := 10; @@ m2 @@ := 20; @[use @m1 and @m2]@ bar;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    ASSERT_EQ(ctx.diag().errorCount(), 0);
    // Ожидаем: ATTR(use, 10, and, 20), ATTR(depend_macro("m1", "m2")), Ident(bar), SEMICOLON
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
    // В первом ATTR раскрылись макросы
    ASSERT_GE(tokens[0]->m_sequence.size(), 4u);
    EXPECT_EQ(tokens[0]->m_sequence[1]->text, "10");
    EXPECT_EQ(tokens[0]->m_sequence[3]->text, "20");
    // Второй ATTR — depend_macro с двумя именами
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::ATTR);
    ASSERT_GE(tokens[1]->m_sequence.size(), 5u);
    EXPECT_EQ(tokens[1]->m_sequence[0]->text, "depend_macro");
    EXPECT_EQ(tokens[1]->m_sequence[1]->kind, ParserToken::Kind::LPAREN);
    EXPECT_EQ(tokens[1]->m_sequence[3]->kind, ParserToken::Kind::COMMA);
}

TEST(MMProcTest, NoMacroInAttrNoDepend) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ dummy @@ := 0; @[const]@ x;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    ASSERT_EQ(ctx.diag().errorCount(), 0);
    // Только ATTR(const), Ident(x), SEMICOLON — никакого depend_macro не добавляется
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[2]->kind, ParserToken::Kind::SEMICOLON);
}

TEST(MMProcTest, MacroInsideAttrWithArgs) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ mul ($x, $y) @@ := @$*; @[calc @mul(3, 4)]@ z;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    ASSERT_EQ(ctx.diag().errorCount(), 0);
    // Ожидаем: ATTR(calc, ...раскрытый mul...), ATTR(depend_macro("mul")), Ident(z), SEMICOLON
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
    // Во втором ATTR — depend_macro с именем "mul"
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::ATTR);
    ASSERT_GE(tokens[1]->m_sequence.size(), 3u);
    EXPECT_EQ(tokens[1]->m_sequence[0]->text, "depend_macro");
}

TEST(MMProcTest, NestedMacroInAttrRecordsBoth) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ inner @@ := 1; @@ outer @@ := @inner; @[data @outer]@ p;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    ASSERT_EQ(ctx.diag().errorCount(), 0);
    // Ожидаем: ATTR(data, 1), ATTR(depend_macro("inner", "outer")), Ident(p), SEMICOLON
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::ATTR);
    // Внутри первого ATTR — раскрытый outer → inner → 1
    ASSERT_GE(tokens[0]->m_sequence.size(), 2u);
    EXPECT_EQ(tokens[0]->m_sequence[1]->text, "1");
    // Во втором ATTR — depend_macro с двумя именами
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::ATTR);
    ASSERT_GE(tokens[1]->m_sequence.size(), 5u);
    EXPECT_EQ(tokens[1]->m_sequence[0]->text, "depend_macro");
}

// ========== Тесты @$... (ellipsis) ==========

TEST(MMProcTest, MacroArgEllipsis) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    // Макрос с variadic (...), @$... в теле подставляет аргументы как отдельные токены с COMMA
    MapperFile idx = ctx.add_source("<test>", "@@ show_all ( ... ) @@ ::= @@ @$... @@; @show_all(a, b);");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    // @$... подставляет все аргументы как отдельные токены: a, COMMA, b
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[0]->text, "a");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[1]->text, ",");
    EXPECT_EQ(tokens[2]->kind, ParserToken::Kind::Ident);
    EXPECT_EQ(tokens[2]->text, "b");
}

// Test: @$... с одним аргументом
TEST(MMProcTest, MacroArgEllipsisSingle) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    // Один аргумент — COMMA не добавляется
    MapperFile idx = ctx.add_source("<test>", "@@ show ( ... ) @@ ::= @@ @$... @@; @show(42);");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::INTEGER);
    EXPECT_EQ(tokens[0]->text, "42");
}

// ========== Тест глубокой рекурсии макросов ==========

TEST(MMProcTest, MacroRecursionDepthLimit) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    // self-referencing macro (бесконечная рекурсия)
    MapperFile idx = ctx.add_source("<test>", "@@ recur @@ := @recur; @recur;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    // Должна быть ошибка превышения глубины рекурсии
    EXPECT_GE(ctx.diag().errorCount(), 1);
}

// ========== Тест конкатенации EMBED-токенов ==========

TEST(MMProcTest, EmbedConcatenation) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "{% alpha %} {% beta %}");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::EMBED);
    EXPECT_EQ(tokens[0]->text, " alpha  beta ");
}

// ========== Тест макроса с LOCAL/NATIVE в многословном имени ==========

TEST(MMProcTest, MacroNameWithLocal) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    // Макрос с LOCAL в имени
    MapperFile idx = ctx.add_source("<test>", "@@ use $local @@ ::= @$local; @use 42;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    // $local — это параметр, имя макроса — "use"
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->text, "42");
}

// ========== Тесты предопределённых макросов @__XXX__ ==========

TEST(MMProcTest, PredefVersionMajor) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TRUST_VERSION_MAJOR__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::IntLiteral);
    EXPECT_EQ(tokens[0]->text, std::to_string(TRUST_VERSION_MAJOR));
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefVersionMinor) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TRUST_VERSION_MINOR__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::IntLiteral);
    EXPECT_EQ(tokens[0]->text, std::to_string(TRUST_VERSION_MINOR));
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefVersionPatch) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TRUST_VERSION_PATCH__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::IntLiteral);
    EXPECT_EQ(tokens[0]->text, std::to_string(TRUST_VERSION_PATCH));
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefVersionString) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TRUST_VERSION__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    EXPECT_EQ(tokens[0]->text, TRUST_VERSION);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefVersionFull) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TRUST_VERSION_FULL__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    EXPECT_EQ(tokens[0]->text, TRUST_VERSION_FULL);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefGitHash) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TRUST_GIT_HASH__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    EXPECT_EQ(tokens[0]->text, TRUST_GIT_HASH);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefDateBuild) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TRUST_DATE_BUILD__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    EXPECT_EQ(tokens[0]->text, TRUST_DATE_BUILD);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefFile) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__FILE__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    EXPECT_EQ(tokens[0]->text, "<test>");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefFileName) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__FILE_NAME__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    EXPECT_EQ(tokens[0]->text, "<test>");
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefLine) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__LINE__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::IntLiteral);
    EXPECT_EQ(tokens[0]->text, "1"); // первая строка
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefFileLine) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__FILE_LINE__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::IntLiteral);
    EXPECT_EQ(tokens[0]->text, "1"); // первая строка
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefFileMd5) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__FILE_MD5__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    // 64-bit hash → 16 hex символов
    EXPECT_EQ(tokens[0]->text.size(), 16u);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefFileTimestamp) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__FILE_TIMESTAMP__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    // ISO timestamp format: YYYY-MM-DDTHH:MM:SSZ
    EXPECT_GE(tokens[0]->text.size(), 20u);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefDate) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__DATE__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    // Формат как __DATE__: Mmm dd yyyy (11 символов)
    EXPECT_EQ(tokens[0]->text.size(), 11u);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefTime) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TIME__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    // Формат как __TIME__: hh:mm:ss (8 символов)
    EXPECT_EQ(tokens[0]->text.size(), 8u);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefTimestamp) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TIMESTAMP__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    // asctime: "Fri 19 Aug 13:32:58 2016" — 24 символа
    EXPECT_EQ(tokens[0]->text.size(), 24u);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefTimestampISO) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__TIMESTAMP_ISO__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::StringLiteral);
    // ISO: "2013-07-06T00:50:06Z" — 20 символов
    EXPECT_EQ(tokens[0]->text.size(), 20u);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
}

TEST(MMProcTest, PredefCounter) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__COUNTER__ @__COUNTER__ @__COUNTER__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::IntLiteral);
    EXPECT_EQ(tokens[0]->text, "0");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::IntLiteral);
    EXPECT_EQ(tokens[1]->text, "1");
    EXPECT_EQ(tokens[2]->kind, ParserToken::Kind::IntLiteral);
    EXPECT_EQ(tokens[2]->text, "2");
}

TEST(MMProcTest, PredefCounterResetBetweenProcess) {
    Context ctx;

    // Первый вызов process
    MapperFile idx1 = ctx.add_source("<test1>", "@__COUNTER__ @__COUNTER__");
    auto lexemes1 = Lexer::tokenize(ctx, idx1);
    auto tokens1 = MMProcessor::process(ctx, lexemes1);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens1.size(), 2u);
    EXPECT_EQ(tokens1[0]->text, "0");
    EXPECT_EQ(tokens1[1]->text, "1");

    // Второй вызов process — счётчик должен сброситься
    MapperFile idx2 = ctx.add_source("<test2>", "@__COUNTER__");
    auto lexemes2 = Lexer::tokenize(ctx, idx2);
    auto tokens2 = MMProcessor::process(ctx, lexemes2);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens2.size(), 1u);
    EXPECT_EQ(tokens2[0]->text, "0");
}

TEST(MMProcTest, PredefUnknownError) {
    Context ctx;
    MapperFile idx = ctx.add_source("<test>", "@__UNKNOWN_MACRO__");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes);
    // Должна быть ошибка
    EXPECT_GE(ctx.diag().errorCount(), 1);
    // Токенов быть не должно (только ошибочный макрос)
    EXPECT_TRUE(tokens.empty());
}

TEST(MMProcTest, PredefNotConflictingWithUserMacro) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ MY_MACRO @@ := hello; @MY_MACRO;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0]->text, "hello");
}

// ========== Тесты нескольких групп скобок в определении макроса ==========

// Test: две группы скобок (круглые + угловые)
TEST(MMProcTest, MacroTwoBracketGroups) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ two ( $a )< $b > @@ ::= @$a @$b; @two(x)<y>;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0]->text, "x");
    EXPECT_EQ(tokens[1]->text, "y");
}

// Test: группы с variadic ... и @$... без цифры
TEST(MMProcTest, MacroTwoVariadicGroupsAll) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    // Две группы с ... : @$... подставляет аргументы из всех вариадических групп
    MapperFile idx = ctx.add_source("<test>", "@@ both ( ... )[ ... ] @@ ::= @@ @$... @@; @both(a, b)[c, d];");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    // @$... подставляет все аргументы из всех вариадических групп: a, COMMA, b, COMMA, c, COMMA, d
    ASSERT_EQ(tokens.size(), 7u);
    EXPECT_EQ(tokens[0]->text, "a");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[2]->text, "b");
    EXPECT_EQ(tokens[3]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[4]->text, "c");
    EXPECT_EQ(tokens[5]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[6]->text, "d");
}

// Test: @$...1 — подстановка только из первой вариадической группы
TEST(MMProcTest, MacroVariadicFirstGroup) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ sel ( ... )[ ... ] @@ ::= @@ @$...1 @@; @sel(a, b)[c, d];");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    // @$...1 подставляет только из первой variadic группы: a, COMMA, b
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->text, "a");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[2]->text, "b");
}

// Test: @$...2 — подстановка только из второй вариадической группы
TEST(MMProcTest, MacroVariadicSecondGroup) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ sel ( ... )[ ... ] @@ ::= @@ @$...2 @@; @sel(a, b)[c, d];");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    // @$...2 подставляет только из второй variadic группы: c, COMMA, d
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->text, "c");
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[2]->text, "d");
}

// Test: @$*.2 — со скобками, вторая группа (не variadic)
TEST(MMProcTest, MacroStarWithSecondGroup) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ sel ( ... )[ ... ] @@ := [@$*.2]; @sel(a, b)[c, d];");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    // @$*.2 → (c , d , ) — внутри скобок, группа 2
    // [@$*.2] → LBRACKET, LPAREN, c, COMMA, d, COMMA, RPAREN, RBRACKET
    ASSERT_EQ(tokens.size(), 8u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::LBRACKET);
    EXPECT_EQ(tokens[1]->kind, ParserToken::Kind::LPAREN);
    EXPECT_EQ(tokens[2]->text, "c");
    EXPECT_EQ(tokens[3]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[4]->text, "d");
    EXPECT_EQ(tokens[5]->kind, ParserToken::Kind::COMMA);
    EXPECT_EQ(tokens[6]->kind, ParserToken::Kind::RPAREN);
    EXPECT_EQ(tokens[7]->kind, ParserToken::Kind::RBRACKET);
}

// Test: три группы разных типов
TEST(MMProcTest, MacroThreeBracketGroups) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ three ( $a )< $b >[ $c ] @@ := @$a @$b @$c; @three(x)<y>[z];");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->text, "x");
    EXPECT_EQ(tokens[1]->text, "y");
    EXPECT_EQ(tokens[2]->text, "z");
}

// Test: квадратные скобки в определении
TEST(MMProcTest, MacroSquareBracketGroup) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ sq [ $x ] @@ := [@$x]; @sq[42];");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::LBRACKET);
    EXPECT_EQ(tokens[1]->text, "42");
    EXPECT_EQ(tokens[2]->kind, ParserToken::Kind::RBRACKET);
}

// Test: угловые скобки в определении
TEST(MMProcTest, MacroAngleBracketGroup) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ ang < $x > @@ := <@$x>; @ang<42>;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0]->kind, ParserToken::Kind::LANGLE);
    EXPECT_EQ(tokens[1]->text, "42");
    EXPECT_EQ(tokens[2]->kind, ParserToken::Kind::RANGLE);
}

// Test: @$name — поиск параметра в его группе
TEST(MMProcTest, MacroParamInOwnGroup) {
    Context ctx;
    auto macros = std::make_shared<MacroTable>();
    MapperFile idx = ctx.add_source("<test>", "@@ test ( $a )< $b > @@ := @$a @$b; @test(x)<y>;");
    auto lexemes = Lexer::tokenize(ctx, idx);
    auto tokens = MMProcessor::process(ctx, lexemes, macros);

    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0]->text, "x");
    EXPECT_EQ(tokens[1]->text, "y");
}

} // namespace trust
