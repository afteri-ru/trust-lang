#include "syntax/macro_test_fixture.hpp"
// -- @__OPTION_PUSH__ / @__OPTION__ / @__OPTION_POP__ --

TEST_F(MacroTest, OptionMacroRedefinedIgnore) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@__OPTION_PUSH__;", macro));
    ASSERT_NO_THROW(Parse("@__OPTION__(\"macro-redefined\", \"ignore\");", macro));

    // Переопределение той же сигнатуры - с ignore молча игнорируется (без диагностики).
    ASSERT_NO_THROW(Parse("@@ A @@ 1 @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ A @@ 2 @@@@;", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();

    ASSERT_NO_THROW(Parse("@__OPTION_POP__;", macro));
}

TEST_F(MacroTest, OptionMacroRedefinedWarningByDefault) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ B @@ 1 @@@@;", macro));
    m_ctx.diag().clear();
    // По умолчанию macro-redefined - предупреждение: переопределение разрешено (last wins).
    ASSERT_NO_THROW(Parse("@@ B @@ 2 @@@@;", macro)) << macro->Dump();
    ASSERT_EQ(0, m_ctx.diag().errorCount());
    ASSERT_GT(m_ctx.diag().warningCount(), 0);
}

TEST_F(MacroTest, OptionMacroRedefinedError) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@__OPTION_PUSH__; @__OPTION__(\"macro-redefined\", \"error\");", macro));
    ASSERT_NO_THROW(Parse("@@ D @@ 1 @@@@;", macro));
    m_ctx.diag().clear();
    // error - переопределение помечается ошибкой (не бросает, но errorCount > 0).
    ASSERT_NO_THROW(Parse("@@ D @@ 2 @@@@;", macro)) << macro->Dump();
    ASSERT_GT(m_ctx.diag().errorCount(), 0);
    ASSERT_NO_THROW(Parse("@__OPTION_POP__;", macro));
}

TEST_F(MacroTest, OptionPushPopRestores) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    // ignore внутри push/pop
    ASSERT_NO_THROW(Parse("@__OPTION_PUSH__;", macro));
    ASSERT_NO_THROW(Parse("@__OPTION__(\"macro-redefined\", \"ignore\");", macro));
    ASSERT_NO_THROW(Parse("@@ C @@ 1 @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ C @@ 2 @@@@;", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    ASSERT_NO_THROW(Parse("@__OPTION_POP__;", macro));

    // после pop настройка восстановлена (по умолчанию - предупреждение)
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@@ D @@ 1 @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ D @@ 2 @@@@;", macro)) << macro->Dump();
}

TEST_F(MacroTest, OptionUnknownOptionError) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@__OPTION__(\"unknown-option\", \"ignore\");", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(MacroTest, OptionUnknownSeverityError) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@__OPTION__(\"macro-redefined\", \"bogus\");", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// -- @__OPTION_TRUE__ / @__OPTION_FALSE__ (условная подстановка по feature-флагу) --

TEST_F(MacroTest, OptionTRUE_FlagEnabled) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, true);

    ASSERT_NO_THROW(Parse("@__OPTION_TRUE__(\"assert\", 42)", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    ASSERT_STREQ("42", LexOut().c_str());
}

TEST_F(MacroTest, OptionTRUE_FlagDisabled) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    // assert выключен явно (по умолчанию включён) → TRUE не срабатывает
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, false);
    ASSERT_FALSE(m_ctx.opts().is_enabled(transpiler::FlagKind::Assert));

    ASSERT_NO_THROW(Parse("@__OPTION_TRUE__(\"assert\", 111)", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    ASSERT_TRUE(LexOut().empty()) << LexOut();
}

TEST_F(MacroTest, OptionFALSE_FlagDisabled) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    // assert выключен → срабатывает FALSE
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, false);
    ASSERT_NO_THROW(Parse("@__OPTION_FALSE__(\"assert\", 777)", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    ASSERT_STREQ("777", LexOut().c_str());
}

TEST_F(MacroTest, OptionFALSE_FlagEnabled) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, true);

    ASSERT_NO_THROW(Parse("@__OPTION_FALSE__(\"assert\", 777)", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    ASSERT_TRUE(LexOut().empty()) << LexOut();
}

// Лексема @\, (COMMA_LEXEME) внутри аргумента прагмы. Поскольку замена @\,→',' выполняется
// ТОЛЬКО при раскрытии макроса, прагма вызывается из тела макроса: `m;` раскрывает макрос,
// при этом @\, превращается в обычную ',' (разделитель аргументов вызова `g(1, 2)`), а прагма
// возвращает этот вызов. В определении макроса вызов записан как `g ( 1 @\, 2 )` (с пробелами и
// сырой лексемой), поэтому подстрока `g(1, 2)` в итоговом AST однозначно соответствует именно
// возвращённому прагмой вызову с настоящей запятой.
TEST_F(MacroTest, OptionTRUE_CommaLexeme) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, true);

    ASSERT_NO_THROW(Parse("@@ m @@ @__OPTION_TRUE__(\"assert\", g( 1 @\\, 2 )) @@@@; m;", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    // TRUE сработал → вернул вызов g(1,2); запятая из @\, стала разделителем двух аргументов.
    ASSERT_NE(std::string::npos, ast->toString().find("g(1, 2)")) << ast->toString();
}

// assert выключен → TRUE не срабатывает: возвращённого вызова g(1, 2) в AST нет.
TEST_F(MacroTest, OptionTRUE_CommaLexemeFlagDisabled) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, false);
    ASSERT_FALSE(m_ctx.opts().is_enabled(transpiler::FlagKind::Assert));

    ASSERT_NO_THROW(Parse("@@ m @@ @__OPTION_TRUE__(\"assert\", g( 1 @\\, 2 )) @@@@; m;", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    ASSERT_EQ(std::string::npos, ast->toString().find("g(1, 2)")) << ast->toString();
}

TEST_F(MacroTest, OptionFALSE_CommaLexeme) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, false);
    ASSERT_FALSE(m_ctx.opts().is_enabled(transpiler::FlagKind::Assert));

    ASSERT_NO_THROW(Parse("@@ m @@ @__OPTION_FALSE__(\"assert\", g( 1 @\\, 2 )) @@@@; m;", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    // FALSE сработал (assert выключен) → вернул вызов g(1,2) с настоящей запятой.
    ASSERT_NE(std::string::npos, ast->toString().find("g(1, 2)")) << ast->toString();
}

// Голой NAME-маркер (`macro_body`/`macro_end` БЕЗ '@') в фазе имени определения макроса должен
// раскрываться так же, как и с '@' - открывать/закрывать тело. Это то, что использует dsl.src:
// `@macro assert( $cond ) macro_body ... macro_end;`. Регрессия: раньше голой NAME-маркер
// игнорировался в фазе имени целиком, из-за чего DSL не грузился (Only one term in a macro
// can have arguments) и падал "Failed to parse DSL source".
TEST_F(MacroTest, BareMarkerInNamePhaseOpensBody) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    // Мнемоники как в dsl.src (тело = ровно одна framing-лексема).
    ASSERT_NO_THROW(Parse("@@ macro @@ @\\@@ @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ macro_body @@ @\\@@ @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ macro_end @@ @\\@4 @@@@;", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();

    // Определение макроса через bare-мнемоники (macro_body/macro_end без '@').
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, true);
    ASSERT_NO_THROW(Parse("@macro assert( $cond ) macro_body\n@__OPTION_TRUE__(\"assert\" @$cond)\n@__OPTION_FALSE__(\"assert\")\nmacro_end;", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();

    // Раскрытие: assert(1>0) с -Wassert -> само условие; тело корректно открыто/закрыто.
    ASSERT_NO_THROW(Parse("assert( 1 > 0 );", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
}

// -- @__OPTION_IIF__(flag, true, false) - две ветки по булевому флагу --

TEST_F(MacroTest, OptionIIF_TrueBranch) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, true);

    ASSERT_NO_THROW(Parse("@__OPTION_IIF__(\"assert\", 42, 111)", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    ASSERT_STREQ("42", LexOut().c_str());
}

TEST_F(MacroTest, OptionIIF_FalseBranch) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    m_ctx.opts().set_enabled(transpiler::FlagKind::Assert, false);
    ASSERT_FALSE(m_ctx.opts().is_enabled(transpiler::FlagKind::Assert));

    ASSERT_NO_THROW(Parse("@__OPTION_IIF__(\"assert\", 42, 111)", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    ASSERT_STREQ("111", LexOut().c_str());
}

// IIF требует ровно 3 аргумента (flag, true, false).
TEST_F(MacroTest, OptionIIF_RequiresThreeArgs) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@__OPTION_IIF__(\"assert\", 1);", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(MacroTest, OptionIIF_UnknownFlag) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@__OPTION_IIF__(\"no-such-flag\", 1, 0);", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// Лексема @\, (COMMA_LEXEME) в теле макроса при раскрытии превращается в обычную запятую.
TEST_F(MacroTest, CommaLexemeInMacroBody) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ f @@ foo( a @\\, b ) @@@@; f;", macro));
    ASSERT_NE(std::string::npos, LexOut().find(',')) << LexOut();
}
