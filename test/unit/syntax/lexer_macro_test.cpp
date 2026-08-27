#include "syntax/lexer_test_fixture.hpp"
TEST_F(Lexer, DocCommentFullText) {
    // Доки сохраняются целиком, включая маркеры (string_view в m_text).
    ASSERT_EQ(1, TokenParse("/** Block doc */"));
    ASSERT_EQ(1, Count(TermID::DOCUMENT));
    EXPECT_EQ("/** Block doc */", std::string(tokens[0]->getText())) << Dump();

    ASSERT_EQ(1, TokenParse("/// line doc"));
    ASSERT_EQ(1, Count(TermID::DOCUMENT));
    EXPECT_EQ("/// line doc", std::string(tokens[0]->getText())) << Dump();

    ASSERT_EQ(2, TokenParse("x ///< trailing"));
    ASSERT_EQ(1, Count(TermID::DOCUMENT));
    EXPECT_EQ("///< trailing", std::string(tokens[1]->getText())) << Dump();

    ASSERT_EQ(1, TokenParse("## hash doc"));
    ASSERT_EQ(1, Count(TermID::DOCUMENT));
    EXPECT_EQ("## hash doc", std::string(tokens[0]->getText())) << Dump();
}

TEST_F(Lexer, Macro) {

    ASSERT_EQ(1, TokenParse("@$arg")) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO_ARGNAME)) << Dump();

    ASSERT_EQ(1, TokenParse("@$1")) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO_ARGPOS)) << Dump();

    //    ASSERT_EQ(1, TokenParse("@$name(*)")) << Dump();
    //    EXPECT_EQ(1, Count(TermID::MACRO_ARGUMENT));
    //    ASSERT_EQ(1, TokenParse("@$name[*]")) << Dump();
    //    EXPECT_EQ(1, Count(TermID::MACRO_ARGUMENT));
    //    ASSERT_EQ(1, TokenParse("@$name<*>")) << Dump();
    //    EXPECT_EQ(1, Count(TermID::MACRO_ARGUMENT));
    //
    //    ASSERT_EQ(1, TokenParse("@$name(#)")) << Dump();
    //    EXPECT_EQ(1, Count(TermID::MACRO_ARGCOUNT));
    //    ASSERT_EQ(1, TokenParse("@$name[#]")) << Dump();
    //    EXPECT_EQ(1, Count(TermID::MACRO_ARGCOUNT));
    //    ASSERT_EQ(1, TokenParse("@$name<#>")) << Dump();
    //    EXPECT_EQ(1, Count(TermID::MACRO_ARGCOUNT));

    ASSERT_EQ(1, TokenParse("@#")) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO_TOSTR));

    ASSERT_EQ(1, TokenParse("@#'")) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO_TOSTR));
    ASSERT_EQ(1, TokenParse("@#\"")) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO_TOSTR));

    ASSERT_EQ(1, TokenParse("@##")) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO_CONCAT));

    ASSERT_EQ(1, TokenParse("@$...")) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO_ARGUMENT));
    ASSERT_EQ(1, TokenParse("@$*")) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO_ARGUMENT));
    ASSERT_EQ(1, TokenParse("@$#")) << Dump();
    EXPECT_EQ(1, Count(TermID::MACRO_ARGCOUNT));

    // seq-макрос: @@ имя @@ тело @@@@;
    ASSERT_EQ(8, TokenParse("@@ abc @@ 123 ... 456 @@@@;")) << Dump();
    EXPECT_EQ(2, Count(TermID::MACRO_SEQ)) << Dump(); // @@ (открытие) и @@ (закрытие имени)
    EXPECT_EQ(1, Count(TermID::MACRO_DEL)) << Dump(); // терминатор @@@@
    EXPECT_EQ(1, Count(TermID::SEMICOLON)) << Dump();

    // text-макрос: @@ имя @@@ тело @@@@;
    ASSERT_EQ(4, TokenParse("@@ abc @@@ 123 ... 456 @@@@;")) << Dump();
    EXPECT_EQ(1, Count(TermID::NAME));
    EXPECT_EQ(1, Count(TermID::MACRO_STR));
    EXPECT_EQ("abc", tokens[1]->getText());
    EXPECT_EQ(" 123 ... 456 ", tokens[2]->getText());
    EXPECT_FALSE(tokens[0]->m_mapperRange.begin.isInvalid()) << Dump();
    EXPECT_FALSE(tokens[2]->m_mapperRange.begin.isInvalid()) << Dump();

    // text-макрос с сигнатурой в скобках: @@ abc ( name ) @@@ тело @@@@;
    ASSERT_EQ(7, TokenParse("@@ abc ( name ) @@@ 123 \n \n ... 456 @@@@;")) << Dump();
    EXPECT_EQ(2, Count(TermID::NAME));
    EXPECT_EQ(1, Count(TermID::LPAREN));
    EXPECT_EQ(1, Count(TermID::RPAREN));
    EXPECT_EQ(1, Count(TermID::MACRO_STR));
    EXPECT_EQ(" 123 \n \n ... 456 ", tokens[5]->getText());
    EXPECT_FALSE(tokens[0]->m_mapperRange.begin.isInvalid());
    EXPECT_FALSE(tokens[5]->m_mapperRange.begin.isInvalid());

    // ASSERT_EQ(6, TokenParse("@if($args) := @@ [@$args] --> @@")) << Dump();
    // EXPECT_EQ(1, Count(TermID::MACRO));
    // EXPECT_EQ(2, Count(TermID::LPAREN) + Count(TermID::RPAREN));
    // EXPECT_EQ(1, Count(TermID::LOCAL));
    // EXPECT_EQ(1, Count(TermID::CREATE_NAME));
    // EXPECT_EQ(1, Count(TermID::MACRO_SEQ));
}

TEST_F(Lexer, Mangled) {
    ASSERT_EQ(1, TokenParse("_$$_123$")) << Dump();
    ASSERT_EQ(1, Count(TermID::MANGLED)) << Dump();

    ASSERT_EQ(1, TokenParse("_$name_$_123$")) << Dump();
    ASSERT_EQ(1, Count(TermID::MANGLED)) << Dump();

    ASSERT_EQ(1, TokenParse("_$na12me_$_name$$$")) << Dump();
    ASSERT_EQ(1, Count(TermID::MANGLED)) << Dump();

    ASSERT_EQ(1, TokenParse("_$na$_12me_$_name$$$")) << Dump();
    ASSERT_EQ(1, Count(TermID::MANGLED)) << Dump();
}

TEST_F(Lexer, MacroDefNewSyntax) {
    // Обычный макрос (тело = последовательность лексем): @@ имя @@ тело @@@@;
    ASSERT_EQ(8, TokenParse("@@ a @@ 1 + 2 @@@@;")) << Dump();
    EXPECT_EQ(2, Count(TermID::MACRO_SEQ)) << Dump(); // открытие, закрытие имени
    EXPECT_EQ(1, Count(TermID::MACRO_DEL)) << Dump(); // конец тела (@@@@)
    EXPECT_EQ(1, Count(TermID::NAME)) << Dump();
    EXPECT_EQ(2, Count(TermID::INTEGER)) << Dump();
    EXPECT_EQ(1, Count(TermID::PLUS)) << Dump();
    EXPECT_EQ(1, Count(TermID::SEMICOLON)) << Dump();
    // Порядок: MACRO_SEQ(open) NAME MACRO_SEQ(close) INTEGER PLUS INTEGER MACRO_DEL(end) ;
    ASSERT_EQ(TermID::MACRO_SEQ, tokens[0]->getTermID());
    ASSERT_EQ(TermID::NAME, tokens[1]->getTermID());
    ASSERT_EQ("a", tokens[1]->getText());
    ASSERT_EQ(TermID::MACRO_SEQ, tokens[2]->getTermID());
    ASSERT_EQ(TermID::INTEGER, tokens[3]->getTermID());
    ASSERT_EQ(TermID::PLUS, tokens[4]->getTermID());
    ASSERT_EQ(TermID::INTEGER, tokens[5]->getTermID());
    ASSERT_EQ(TermID::MACRO_DEL, tokens[6]->getTermID()); // конец seq-тела (@@@@)
    ASSERT_EQ(TermID::SEMICOLON, tokens[7]->getTermID());
}

TEST_F(Lexer, MacroDefNewSyntaxSignatureSeq) {
    // Сигнатура - последовательность термов (с заместителями и символами):
    // func $name ( ... ): $... {  + закрытие имени + тело + конец тела + ;
    ASSERT_EQ(13, TokenParse("@@ func $name ( ... ): $... { @@ body @@@@;")) << Dump();
    ASSERT_EQ(TermID::MACRO_SEQ, tokens[0]->getTermID()); // открытие
    ASSERT_EQ(TermID::NAME, tokens[1]->getTermID());      // func
    ASSERT_EQ(TermID::LOCAL, tokens[2]->getTermID());     // $name
    ASSERT_EQ(TermID::LPAREN, tokens[3]->getTermID());
    ASSERT_EQ(TermID::ELLIPSIS, tokens[4]->getTermID());
    ASSERT_EQ(TermID::RPAREN, tokens[5]->getTermID());
    ASSERT_EQ(TermID::COLON, tokens[6]->getTermID());  // :
    ASSERT_EQ(TermID::LOCAL, tokens[7]->getTermID());  // $...
    ASSERT_EQ(TermID::LBRACE, tokens[8]->getTermID()); // {
}

TEST_F(Lexer, MacroDefNewSyntaxStr) {
    // Текстовый макрос (тело = строка): @@ имя @@@ текст @@@@;
    ASSERT_EQ(4, TokenParse("@@ a @@@ some text @@@@;")) << Dump();
    ASSERT_EQ(TermID::MACRO_SEQ, tokens[0]->getTermID()); // открытие
    ASSERT_EQ(TermID::NAME, tokens[1]->getTermID());      // a
    ASSERT_EQ(TermID::MACRO_STR, tokens[2]->getTermID()); // текст
    ASSERT_EQ(" some text ", tokens[2]->getText());
    ASSERT_EQ(TermID::SEMICOLON, tokens[3]->getTermID());
}

TEST_F(Lexer, MacroFramingLexemes) {
    // Лексемы-представители маркеров (@\@@/@\@@@/@\@@@@) - инертные токены внутри тел макросов.
    // Распознавание маркеров по имени выполняется в Parser::GetNextToken (Macro::MarkerToken),
    // а не в лексере, поэтому здесь проверяем только лексику.
    ASSERT_EQ(1, TokenParse("@\\@@")) << Dump();
    ASSERT_EQ(TermID::MACRO_LEXEME, tokens[0]->getTermID());

    ASSERT_EQ(1, TokenParse("@\\@@@")) << Dump();
    ASSERT_EQ(TermID::MACRO_STR_LEXEME, tokens[0]->getTermID());

    ASSERT_EQ(1, TokenParse("@\\@@@@")) << Dump();
    ASSERT_EQ(TermID::MACRO_DEL_LEXEME, tokens[0]->getTermID());

    // Эскейп-лексема `@\@4` (замена `@@@@`) - алиас `@\@@@@` → тот же MACRO_DEL_LEXEME.
    ASSERT_EQ(1, TokenParse("@\\@4")) << Dump();
    ASSERT_EQ(TermID::MACRO_DEL_LEXEME, tokens[0]->getTermID());

    // Лексема не должна поглощать «похожие» последовательности: @\@@x = MACRO_LEXEME + NAME.
    ASSERT_EQ(2, TokenParse("@\\@@x")) << Dump();
    ASSERT_EQ(TermID::MACRO_LEXEME, tokens[0]->getTermID());
    ASSERT_EQ(TermID::NAME, tokens[1]->getTermID());

    // Мнемоники больше НЕ резервируются в лексере: это обычные MACRO-токены (маркер - по имени в парсере).
    ASSERT_EQ(1, TokenParse("@macro")) << Dump();
    ASSERT_EQ(TermID::MACRO, tokens[0]->getTermID());
    ASSERT_EQ(1, TokenParse("@macro_body")) << Dump();
    ASSERT_EQ(TermID::MACRO, tokens[0]->getTermID());
    ASSERT_EQ(1, TokenParse("@macro_text")) << Dump();
    ASSERT_EQ(TermID::MACRO, tokens[0]->getTermID());
    ASSERT_EQ(1, TokenParse("@macro_end")) << Dump();
    ASSERT_EQ(TermID::MACRO, tokens[0]->getTermID());
    ASSERT_EQ(1, TokenParse("@macro_del")) << Dump();
    ASSERT_EQ(TermID::MACRO, tokens[0]->getTermID());
    ASSERT_EQ(1, TokenParse("@macro_bodyx")) << Dump();
    ASSERT_EQ(TermID::MACRO, tokens[0]->getTermID());
}

TEST_F(Lexer, MacroDelNewSyntax) {
    // Удаление макроса: @@ имя @@@@;
    ASSERT_EQ(4, TokenParse("@@ alias @@@@;")) << Dump();
    ASSERT_EQ(TermID::MACRO_SEQ, tokens[0]->getTermID()); // открытие @@
    ASSERT_EQ(TermID::NAME, tokens[1]->getTermID());
    ASSERT_EQ(TermID::MACRO_DEL, tokens[2]->getTermID()); // терминатор/удаление @@@@
    ASSERT_EQ(TermID::SEMICOLON, tokens[3]->getTermID());
}

TEST_F(Lexer, MacroBodyWithBraceNoBraceStack) {
    // `{`/`}` внутри тела макроса не должны трогать brace-стек.
    ASSERT_EQ(7, TokenParse("@@ f @@ { } @@@@;")) << Dump();
    ASSERT_EQ(TermID::MACRO_SEQ, tokens[0]->getTermID());
    ASSERT_EQ(TermID::NAME, tokens[1]->getTermID());
    ASSERT_EQ(TermID::MACRO_SEQ, tokens[2]->getTermID()); // имя закрыто
    ASSERT_EQ(TermID::LBRACE, tokens[3]->getTermID());
    ASSERT_EQ(TermID::RBRACE, tokens[4]->getTermID());
    ASSERT_EQ(TermID::MACRO_DEL, tokens[5]->getTermID()); // конец тела (@@@@)
}

TEST_F(Lexer, MacroBodyArithmeticAndLiterals) {
    // Тело с арифметикой, литералами, строками и кортежем.
    ASSERT_EQ(15, TokenParse("@@ r @@ 1 + 2.5 'str' ( a , b , ) @@@@;")) << Dump();
    ASSERT_EQ(TermID::INTEGER, tokens[3]->getTermID());
    ASSERT_EQ(TermID::NUMBER, tokens[5]->getTermID());
    ASSERT_EQ(TermID::STRCHAR, tokens[6]->getTermID());
    ASSERT_EQ(TermID::LPAREN, tokens[7]->getTermID());
}

TEST_F(Lexer, ParseLexem) {
    Macro macro(ctx);

    SequenceType arr = Scanner::ParseLexem(ctx, "1 2 3 4 5");

    ASSERT_EQ(5, arr.size()) << macro.DumpText(arr).c_str();
    ASSERT_EQ("1 2 3 4 5", macro.DumpText(arr));

    arr = Scanner::ParseLexem(ctx, "macro    @test(1,2,3,...):type; next \n; # sssssss\n @only lexem((((;;     ;");
    ASSERT_EQ("macro @test ( 1 , 2 , 3 , ... ) : type ; next ; @only lexem ( ( ( ( ; ; ;", macro.DumpText(arr));
}

// -- Trust-маркеры (единая форма): @{ ... @} / @{ kind: ... @} --
// Лексер выдаёт только маркеры начала/конца (TRUST_BEGIN/END); содержимое - обычные токены.
