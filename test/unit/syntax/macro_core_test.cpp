#include "syntax/macro_test_fixture.hpp"
TEST_F(MacroTest, ParseTerm) {

    TermPtr term;
    SequenceType buff;
    size_t size;

    buff.push_back(Term::Create(TermID::NAME, "alias", {}, parser::token_type::NAME)); // alias

    ASSERT_NO_THROW(size = Parser::ParseTerm(term, buff, m_ctx, 0));
    ASSERT_EQ(1, size);
    ASSERT_TRUE(term);
    ASSERT_FALSE(term->isCall());
    ASSERT_EQ("alias", term->toString());

    buff.push_back(Term::Create(TermID::NAME, "alias", {}, parser::token_type::NAME)); // alias alias

    ASSERT_NO_THROW(size = Parser::ParseTerm(term, buff, m_ctx, 0));
    ASSERT_EQ(1, size);
    ASSERT_TRUE(term);
    ASSERT_FALSE(term->isCall());
    ASSERT_EQ("alias", term->toString());

    buff.push_back(Term::Create(TermID::NAME, "second", {}, parser::token_type::NAME)); // alias alias second

    ASSERT_NO_THROW(size = Parser::ParseTerm(term, buff, m_ctx, 0));
    ASSERT_EQ(1, size);
    ASSERT_TRUE(term);
    ASSERT_FALSE(term->isCall());
    ASSERT_EQ("alias", term->toString());

    buff.erase(buff.begin(), buff.begin() + 2);
    buff.push_back(Term::Create(TermID::LPAREN, "(", {}, parser::token_type::LPAREN)); // second (

    ASSERT_ANY_THROW(Parser::ParseTerm(term, buff, m_ctx, 0));

    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN)); // second ( )

    ASSERT_NO_THROW(size = Parser::ParseTerm(term, buff, m_ctx, 0));
    ASSERT_EQ(3, size);
    ASSERT_TRUE(term);
    ASSERT_TRUE(term->isCall());
    ASSERT_EQ("second()", term->toString());

    buff.erase(buff.end()); // second (

    buff.push_back(Term::Create(TermID::NAME, "name", {}, parser::token_type::NAME)); // second ( name
    ASSERT_ANY_THROW(Parser::ParseTerm(term, buff, m_ctx, 0));

    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN)); // second ( name )

    ASSERT_NO_THROW(size = Parser::ParseTerm(term, buff, m_ctx, 0));
    ASSERT_EQ(4, size);
    ASSERT_TRUE(term);
    ASSERT_TRUE(term->isCall());
    ASSERT_EQ("second(name)", term->toString());

    buff.erase(buff.end());                                                    // second ( name
    buff.push_back(Term::Create(TermID::EQ, "=", {}, parser::token_type::EQ)); // second ( name =
    ASSERT_ANY_THROW(Parser::ParseTerm(term, buff, m_ctx, 0));

    buff.push_back(Term::Create(TermID::NAME, "value", {}, parser::token_type::NAME)); // second ( name = value
    ASSERT_ANY_THROW(Parser::ParseTerm(term, buff, m_ctx, 0));

    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN)); // second ( name = value )

    ASSERT_NO_THROW(size = Parser::ParseTerm(term, buff, m_ctx, 0));
    ASSERT_EQ(6, size);
    ASSERT_TRUE(term);
    ASSERT_TRUE(term->isCall());
    ASSERT_EQ(1, term->size());
    ASSERT_STREQ("name", term->at(0).first.c_str());
    ASSERT_EQ("second(name=value)", term->toString());

    buff = Scanner::ParseLexem(m_ctx, "second2 ( 1 , ( 123 , ) );\n\n\n\n;");

    ASSERT_NO_THROW(size = Parser::ParseTerm(term, buff, m_ctx, 0));
    ASSERT_EQ(9, size);
    ASSERT_TRUE(term);
    ASSERT_TRUE(term->isCall());
    ASSERT_EQ(2, term->size());
    ASSERT_EQ("1", term->at(0).second->toString());
    ASSERT_EQ("(123,)", term->at(1).second->toString());
    ASSERT_EQ("second2(1, (123,))", term->toString());
}

TEST_F(MacroTest, Pragma) {

    ASSERT_NO_THROW(Parse("; 100")) << LexOut().c_str();
    ASSERT_NO_THROW(Parse("; ; ; ; 100; ")) << LexOut().c_str();
}

// @__PRAGMA_MESSAGE__ выводится через diag (Severity::Note) с местом прагмы, а не в stderr.
TEST_F(MacroTest, PragmaMessage_UsesDiag) {
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@__PRAGMA_MESSAGE__( \"Hello \", \"world\" )"));
    const auto& diags = m_ctx.diag().diagnostics();
    ASSERT_FALSE(diags.empty());
    const auto& d = diags.front();
    EXPECT_EQ(d.severity, Severity::Note);
    EXPECT_EQ(std::string(d.message), "Hello world");
    EXPECT_FALSE(d.range.isInvalid()) << "pragma message must carry a diagnostic location";
}

// @__PRAGMA_WARNING__ выводится через diag (Severity::Warning) с местом прагмы.
TEST_F(MacroTest, PragmaWarning_UsesDiag) {
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@__PRAGMA_WARNING__( \"careful\" )"));
    const auto& diags = m_ctx.diag().diagnostics();
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags.front().severity, Severity::Warning);
    EXPECT_EQ(std::string(diags.front().message), "careful");
    EXPECT_FALSE(diags.front().range.isInvalid());
}

// @__PRAGMA_ERROR__ выводится мягкой ошибкой через diag (Severity::Error), без исключения.
TEST_F(MacroTest, PragmaError_UsesDiag) {
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@__PRAGMA_ERROR__( \"boom\" )"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    const auto& diags = m_ctx.diag().diagnostics();
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags.front().severity, Severity::Error);
    EXPECT_EQ(std::string(diags.front().message), "boom");
    EXPECT_FALSE(diags.front().range.isInvalid());
}

// @__PRAGMA_EXPECTED__: следующий токен входит в список - ожидаемая-диагностика отсутствует.
TEST_F(MacroTest, PragmaExpected_NextTokenAccepts) {
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@__PRAGMA_EXPECTED__( \"=>\", \"==>\" ) ==> x;"));
    for (const auto& d : m_ctx.diag().diagnostics()) {
        EXPECT_EQ(std::string(d.message).find("expected one of"), std::string::npos) << d.message;
    }
}

// @__PRAGMA_EXPECTED__: следующий токен НЕ входит в список - ошибка с перечислением ожидаемых.
TEST_F(MacroTest, PragmaExpected_NextTokenRejects) {
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@__PRAGMA_EXPECTED__( \"=>\", \"==>\" ) : x;"));
    bool found = false;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (std::string(d.message).find("expected one of") != std::string::npos) {
            found = true;
            EXPECT_NE(std::string(d.message).find("=>"), std::string::npos) << d.message;
            EXPECT_NE(std::string(d.message).find("==>"), std::string::npos) << d.message;
            EXPECT_NE(std::string(d.message).find("found ':'"), std::string::npos) << d.message;
        }
    }
    EXPECT_TRUE(found) << "expected-list diagnostic must be present";
}

// @__PRAGMA_EXPECTED__ в теле макроса match( ... ): следующий токен обязан быть одним из
// ожидаемых (=>, ==>, ===>, ~>, ~~>, ~~~>). Совпадение (==>) - без ошибки; несовпадение (:) - ошибка.
TEST_F(MacroTest, PragmaExpected_MatchMacro) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ match( ... ) @@ [ @$... ] @__PRAGMA_EXPECTED__( \"=>\", \"==>\", \"===>\", \"~>\", \"~~>\", \"~~~>\" ) @@@@;", macro));

    // Правильный токен (==> из списка) - ожидаемая-диагностика отсутствует.
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@match( x ) ==> { }", macro));
    for (const auto& d : m_ctx.diag().diagnostics()) {
        EXPECT_EQ(std::string(d.message).find("expected one of"), std::string::npos) << d.message;
    }

    // Неожиданный токен (:) - ошибка с перечислением ожидаемых.
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@match( x ) : y", macro));
    bool found = false;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (std::string(d.message).find("expected one of") != std::string::npos) {
            found = true;
            EXPECT_NE(std::string(d.message).find("=>"), std::string::npos) << d.message;
            EXPECT_NE(std::string(d.message).find("===>"), std::string::npos) << d.message;
            EXPECT_NE(std::string(d.message).find("found ':'"), std::string::npos) << d.message;
        }
    }
    EXPECT_TRUE(found) << "expected-list diagnostic must be present";
}

TEST_F(MacroTest, DISABLED_Annotate) {

    // Logger callback removed - errors are printed to stderr, not captured in m_output.
    // These tests verify the annotation pragma behavior through parser exceptions and LexOut.

    ASSERT_ANY_THROW(Parse("@__ANNOTATION_SET__"));
    ASSERT_STREQ("", LexOut().c_str());

    ASSERT_ANY_THROW(Parse("@__ANNOTATION_SET__()"));
    ASSERT_STREQ("", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__ANNOTATION_SET__(name)"));
    ASSERT_STREQ("", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__ANNOTATION_SET__(name, \"value\")"));
    ASSERT_STREQ("", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__ANNOTATION_SET__(name, 1)"));
    ASSERT_STREQ("", LexOut().c_str());

    ASSERT_ANY_THROW(Parse("@__ANNOTATION_IIF__"));
    ASSERT_STREQ("", LexOut().c_str());

    ASSERT_ANY_THROW(Parse("@__ANNOTATION_IIF__()"));
    ASSERT_STREQ("", LexOut().c_str());

    ASSERT_ANY_THROW(Parse("@__ANNOTATION_IIF__(name)"));
    ASSERT_STREQ("", LexOut().c_str());

    ASSERT_ANY_THROW(Parse("@__ANNOTATION_IIF__(name, 1)"));
    ASSERT_STREQ("", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__ANNOTATION_SET__(name, 1)    @__ANNOTATION_IIF__(name, 1, 2)"));
    ASSERT_STREQ("1", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__ANNOTATION_SET__(name, 0)   @__ANNOTATION_IIF__(name, 1, 2)"));
    ASSERT_STREQ("2", LexOut().c_str());
}

TEST_F(MacroTest, Buffer) {

    TermPtr term;
    SequenceType buffer;
    MacroPtr macro_buf = std::make_shared<Macro>(m_ctx);
    // Тест фокусируется на лексике/раскрытии; операторная семантика `:=`/`::=`/`=` убрана,
    // повторные определения имён — молча (как было с `:=`/`=`).
    ASSERT_NO_THROW(Parse("@__OPTION_PUSH__; @__OPTION__(\"macro-redefined\", \"ignore\");", macro_buf));

    ASSERT_STREQ("name", macro_buf->toMacroHashName("name").c_str());
    ASSERT_STREQ("$", macro_buf->toMacroHashName("$name").c_str());
    ASSERT_STREQ("name", macro_buf->toMacroHashName("@name").c_str());

    ASSERT_FALSE(macro_buf->IdentityMacro(buffer, term));

#define CREATE_TERM(type, text) Term::Create(TermID::type, text, {}, parser::token_type::type)

    term = Parse("@@ macro @@ name @@@@", macro_buf);
    ASSERT_TRUE(term);
    ASSERT_TRUE(term->isMacro());
    ASSERT_TRUE(term->m_left);

    buffer.push_back(CREATE_TERM(NAME, "macro"));
    ASSERT_TRUE(macro_buf->IdentityMacro(buffer, term));

    // Входной буфер больше
    buffer.push_back(CREATE_TERM(NAME, "macro2"));
    ASSERT_TRUE(macro_buf->IdentityMacro(buffer, term));

    // Разные имена терминов
    term->getText() = "macro2";
    term->m_left->m_sequence[0]->getText() = "macro2";
    ASSERT_FALSE(macro_buf->IdentityMacro(buffer, term));

    ASSERT_EQ(2, buffer.size());
    buffer.erase(buffer.begin(), buffer.begin() + 1);
    ASSERT_EQ(1, buffer.size());
    ASSERT_TRUE(macro_buf->IdentityMacro(buffer, term));

    TermPtr hash = Parse("@@ name1 name2 @@   @@@@", macro_buf);
    ASSERT_TRUE(hash);
    ASSERT_TRUE(hash->isMacro());
    ASSERT_TRUE(hash->m_left);
    ASSERT_TRUE(hash->m_right);

    ASSERT_STREQ("name1", macro_buf->toMacroHash(hash).c_str());

    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_TRUE(macro->isEmpty());

    ASSERT_TRUE(term = Parse("@@alias@@ alias_name @@@@;", macro));
    ASSERT_STREQ("@@ alias @@ alias_name @@@@ ;", LexOut().c_str());
    ASSERT_TRUE(term);
    ASSERT_EQ("@@ alias @@", term->toString());
    ASSERT_TRUE(term->m_left->m_sequence[0]);
    ASSERT_EQ(1, term->m_left->m_sequence.size());
    ASSERT_TRUE(term->m_left->m_sequence[0]);
    ASSERT_EQ("alias", term->m_left->m_sequence[0]->toString());

    SequenceType id = macro->GetMacroId(macro->FindMacroList("alias")->at(0));
    ASSERT_EQ(1, id.size()) << macro->FindMacroList("alias")->at(0)->toString().c_str();
    ASSERT_EQ("alias", id[0]->getText());

    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias"})) << macro->Dump();

    // FAIL REDEFINE
    ASSERT_NO_THROW(Parse("@@alias@@ alias_name2 @@@@;", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    //    ASSERT_TRUE(macro->GetMacro({"alias"})) << macro->Dump();
    //    ASSERT_STREQ("@@ alias @@ alias_name2  @@@@;", LexOut().c_str());

    //    ASSERT_ANY_THROW(Parse("@alias+alias := alias_name", macro)) << macro->Dump();
    //    ASSERT_EQ(1, macro->GetCount());

    ASSERT_TRUE(term = Parse("@@alias2@@ alias_name @@@@;", macro));
    ASSERT_EQ(2, macro->CountInScope(0));
    ASSERT_STREQ("@@ alias2 @@ alias_name @@@@ ;", LexOut().c_str());

    ASSERT_TRUE(term->m_left);
    ASSERT_EQ(1, term->m_left->m_sequence.size());
    ASSERT_TRUE(term->m_left->m_sequence[0]);
    ASSERT_EQ("alias2", term->m_left->m_sequence[0]->toString());

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@ @@@@;", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);

    ASSERT_TRUE(Parse("@@ alias @@@@;", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    // удаление alias в новом формате; LexOut см. ниже

    ASSERT_TRUE(Parse("@@ _ @@@@;", macro));
    ASSERT_EQ(0, macro->CountInScope(0)) << macro->Dump();
    // удаление _ в новом формате

    ASSERT_TRUE(term = Parse("@@if(args)@@ [@$args] --> @@@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_STREQ("@@ if ( args ) @@ [ @$args ] --> @@@@", LexOut().c_str());

    ASSERT_TRUE(term->m_left);
    ASSERT_EQ(4, term->m_left->m_sequence.size());
    ASSERT_EQ(1, macro->GetMacroId(term).size());
    ASSERT_EQ("if(args)", macro->GetMacroId(term)[0]->toString());

    id = macro->GetMacroId(macro->FindMacroList("if")->at(0));
    ASSERT_EQ(1, id.size()) << macro->FindMacroList("if")->at(0)->toString().c_str();
    ASSERT_EQ("if", id[0]->getText());

    ASSERT_TRUE(macro->GetMacro({"if"})) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"if"})->m_right);
    ASSERT_EQ(4, macro->GetMacro({"if"})->m_right->m_sequence.size()) << macro->GetMacro({"if"})->m_right->toString().c_str();

    ASSERT_TRUE(term = Parse("@@if2(...)@@@ [ @__LINE__ ] --> @@@@", macro));
    ASSERT_STREQ("@@ if2 ( ... ) @@  [ @__LINE__ ] -->  @@@@", LexOut().c_str());

    ASSERT_TRUE(term = Parse("@@if2(...)@@ [ @__LINE__ ] --> @@@@", macro));
    ASSERT_STREQ("@@ if2 ( ... ) @@ [ @__LINE__ ] --> @@@@", LexOut().c_str());

    ASSERT_TRUE(term->m_left);
    ASSERT_EQ(4, term->m_left->m_sequence.size());
    SequenceType id1 = macro->GetMacroId(term);
    ASSERT_EQ(1, id1.size());
    ASSERT_TRUE(id1[0]);
    ASSERT_EQ("if2(...)", id1[0]->toString());

    ASSERT_EQ(2, macro->CountInScope(0));
    ASSERT_TRUE(macro->GetMacro({"if2"}));
    ASSERT_TRUE(macro->GetMacro({"if2"})->m_right);
    ASSERT_EQ("@@ [ @__LINE__ ] --> @@", macro->GetMacro({"if2"})->m_right->toString());

    ASSERT_TRUE(term = Parse("@@ func $name(arg= @__LINE__ , ...) @@@ [ @__LINE__ ] --> @@@@", macro));
    ASSERT_STREQ("@@ func $name ( arg = @__LINE__ , ... ) @@  [ @__LINE__ ] -->  @@@@", LexOut().c_str());

    SequenceType id2 = macro->GetMacroId(term);
    ASSERT_EQ(2, id2.size());
    ASSERT_TRUE(id2[0]);
    ASSERT_EQ("func", id2[0]->toString());
    ASSERT_TRUE(id2[1]);
    // Сигнатура хранится «как написана»: преdef-макрос @__LINE__ в дефолте аргумента НЕ раскрывается
    // на этапе определения (это делает ParseText через ParseTerm; прямое построение MakeMacroId из
    // лексем сохраняет исходный токен). Раскрытие происходит при раскрытии макроса. Согласовано
    // со строкой LexOut() определения выше (`arg = @__LINE__` без подстановки).
    ASSERT_EQ("$name(arg=@__LINE__, ...)", id2[1]->toString());

    ASSERT_EQ(3, macro->CountInScope(0));
    ASSERT_TRUE(macro->GetMacro(std::vector<std::string>({"func", "$"})));
    ASSERT_TRUE(macro->GetMacro(std::vector<std::string>({"func", "$"}))->m_right);
    ASSERT_EQ("@@@ [ @__LINE__ ] --> @@@@", macro->GetMacro(std::vector<std::string>({"func", "$"}))->m_right->toString());

#undef CREATE_TERM
}

TEST_F(MacroTest, ScopeStack) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    // Изначально только базовый скоуп.
    ASSERT_EQ(1, macro->ScopeCount());
    ASSERT_TRUE(macro->isEmpty());

    macro->PushScope();
    ASSERT_EQ(2, macro->ScopeCount());
    ASSERT_TRUE(macro->isEmpty());

    // Макрос создаётся в верхнем (текущем) скоупе.
    ASSERT_NO_THROW(Parse("@@a@@ 1 @@@@;", macro));
    ASSERT_FALSE(macro->isEmpty());
    ASSERT_EQ(0, macro->CountInScope(0));
    ASSERT_EQ(1, macro->CountInScope(1));

    // Переопределение (`@a = ...`) обновляет макрос в том же скоупе, где он был определён.
    ASSERT_NO_THROW(Parse("@@a@@ = 2;", macro));
    ASSERT_EQ(1, macro->CountInScope(1));
    ASSERT_EQ(0, macro->CountInScope(0));

    // PopScope удаляет верхний скоуп и все его макросы.
    macro->PopScope();
    ASSERT_EQ(1, macro->ScopeCount());
    ASSERT_TRUE(macro->isEmpty());
}

TEST_F(MacroTest, MacroMacro) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_TRUE(macro->isEmpty());

    ASSERT_TRUE(Parse("@@alias replace@@ replace @@@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(Parse("@@alias second@@ second @@@@", macro));
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(Parse("@@text@@@ text;\n text @@@@", macro));
    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(Parse("@@dsl@@@ \n @@m1@@ := @@mm@@;\n  @@m2@@ := @@mm@@;\n @@@@", macro));

    ASSERT_EQ(4, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias", "replace"}));
    TermPtr macro_replace = macro->GetMacro({"alias", "replace"});
    ASSERT_TRUE(macro->GetMacro({"alias", "second"}));
    TermPtr macro_second = macro->GetMacro({"alias", "second"});
    ASSERT_TRUE(macro->GetMacro({"text"}));
    TermPtr macro_text = macro->GetMacro({"text"});
    ASSERT_TRUE(macro->GetMacro({"dsl"}));
    TermPtr macro_dsl = macro->GetMacro({"dsl"});

    TermPtr term = Term::Create(TermID::NAME, "alias", {}, parser::token_type::NAME);

    ASSERT_TRUE(macro->GetMacro({"alias", "replace"}));
    ASSERT_TRUE(macro->GetMacro({"alias", "second"}));

    SequenceType buff;
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_replace)); // alias replace
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second));  // alias second
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));    // text
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));     // dsl

    buff.push_back(term); // alias

    ASSERT_FALSE(macro->IdentityMacro(buff, macro_replace)); // alias replace
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second));  // alias second
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));    // text
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));     // dsl

    buff.push_back(term); // alias alias

    ASSERT_FALSE(macro->IdentityMacro(buff, macro_replace)); // alias replace
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second));  // alias second
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));    // text
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));     // dsl

    buff.push_back(Term::Create(TermID::NAME, "second", {}, parser::token_type::NAME)); // alias alias second

    ASSERT_FALSE(macro->IdentityMacro(buff, macro_replace)); // alias replace
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second));  // alias second
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));    // text
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));     // dsl

    buff.push_back(Term::Create(TermID::LPAREN, "(", {}, parser::token_type::LPAREN)); // alias alias second (

    ASSERT_FALSE(macro->IdentityMacro(buff, macro_replace)); // alias replace
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second));  // alias second
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));    // text
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));     // dsl

    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN)); // alias alias second ( )

    ASSERT_FALSE(macro->IdentityMacro(buff, macro_replace)); // alias replace
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second));  // alias second
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));    // text
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));     // dsl

    buff.erase(buff.begin()); // alias second ( )

    ASSERT_FALSE(macro->IdentityMacro(buff, macro_replace)); // alias replace
    ASSERT_TRUE(macro->IdentityMacro(buff, macro_second));   // alias second
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));    // text
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));     // dsl

    //    ASSERT_TRUE(Parse("alias", macro));
    //    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    //    ASSERT_STREQ("alias", ast->toString().c_str());

    ASSERT_TRUE(Parse("alias replace", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("replace", ast->getText());

    ASSERT_TRUE(Parse("alias second", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("second", ast->getText());

    ASSERT_EQ(4, macro->CountInScope(0));
    ASSERT_FALSE(macro->GetMacro({"m1"})) << macro->Dump();
    ASSERT_FALSE(macro->GetMacro({"m2"})) << macro->Dump();

    //@todo Bug: https://github.com/rsashka/newlang/issues/22
    //    ASSERT_TRUE(Parse("dsl", macro));
    //
    //    ASSERT_EQ(6, macro->GetCount());
    //    ASSERT_TRUE(macro->GetMacro({"m1"})) << macro->Dump();
    //    ASSERT_TRUE(macro->GetMacro({"m2"})) << macro->Dump();
}

TEST_F(MacroTest, Simple) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_TRUE(macro->isEmpty());

    ASSERT_NO_THROW(Parse("@@alias@@ replace @@@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    // Переопределение ТОЙ ЖЕ сигнатуры (alias() → сигнатура alias): по умолчанию предупреждение.
    ASSERT_NO_THROW(Parse("@@alias()@@ replace @@@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@alias(...)@@ error @@@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@second(...)@@ second2(@$#, @$...) @@@@", macro));
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();

    // second() и second - одна сигнатура (скобки-аргументы не меняют идентификатор):
    // повторное определение - по умолчанию предупреждение (last wins).
    ASSERT_NO_THROW(Parse("@@second@@ second2(@$#, @$*) @@@@", macro));
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@text(...)@@ text1(@$#, @$*) @@@@", macro));
    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();
    ASSERT_NO_THROW(Parse("@@dsl@@@ \n @@m1@@ := @@mm@@;\n @@m2@@ := @@mm@@;\n @@@@", macro));

    ASSERT_EQ(4, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias"}));
    TermPtr macro_alias = macro->GetMacro({"alias"});
    ASSERT_TRUE(macro->GetMacro({"second"}));
    TermPtr macro_second = macro->GetMacro({"second"});
    ASSERT_TRUE(macro->GetMacro({"text"}));
    TermPtr macro_text = macro->GetMacro({"text"});
    ASSERT_TRUE(macro->GetMacro({"dsl"}));
    TermPtr macro_dsl = macro->GetMacro({"dsl"});

    TermPtr term = Term::Create(TermID::NAME, "alias", {}, parser::token_type::NAME);

    ASSERT_TRUE(macro->GetMacro({"alias"}));
    ASSERT_TRUE(macro->GetMacro({"second"}));

    SequenceType buff;                                      //
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_alias));  // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl

    buff.push_back(term); // alias

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));   // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl

    buff.push_back(term); // alias alias

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));   // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl

    buff.push_back(Term::Create(TermID::NAME, "second", {}, parser::token_type::NAME)); // alias alias second

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));   // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl

    buff.push_back(Term::Create(TermID::LPAREN, "(", {}, parser::token_type::LPAREN)); // alias alias second (

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));   // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl

    buff.push_back(Term::Create(TermID::NAME, "arg", {}, parser::token_type::NAME)); // alias alias second ( arg

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));   // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl()

    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN)); // alias alias second ( arg )

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));   // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl

    buff.erase(buff.begin()); // alias second ( arg )

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));   // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl

    buff.erase(buff.begin()); // second ( arg )

    ASSERT_FALSE(macro->IdentityMacro(buff, macro_alias)); // alias
    ASSERT_TRUE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));  // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));   // dsl

    ASSERT_NO_THROW(Parse("@alias", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("error", ast->toString());

    ASSERT_NO_THROW(Parse("alias", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("error", ast->getText());

    ASSERT_NO_THROW(Parse("second()", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("second2", ast->getText());

    ASSERT_NO_THROW(Parse("@second()", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("second2", ast->getText());

    ASSERT_NO_THROW(Parse("second(123)", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(2, ast->size()) << LexOut();
    ASSERT_EQ("second2(1, (123,))", ast->toString());

    ASSERT_NO_THROW(Parse("@second(123, 456)", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("second2(2, (123, 456,))", ast->toString());

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("second", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@second", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);

    //    ASSERT_ANY_THROW(Parse("text", macro));
    //    ASSERT_NO_THROW(Parse("text()", macro));
    //    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    //    ASSERT_STREQ("text1(0, (,))", ast->toString().c_str());
    //
    //    ASSERT_NO_THROW(Parse("text(123)", macro));
    //    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    //    ASSERT_STREQ("text1(1, (123,) )", ast->toString().c_str());
    //
    //    ASSERT_NO_THROW(Parse("text(123, 456)", macro));
    //    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    //    ASSERT_STREQ("text1(2, (123, 456,))", ast->toString().c_str());

    ASSERT_EQ(4, macro->CountInScope(0));
    ASSERT_FALSE(macro->GetMacro({"m1"})) << macro->Dump();
    ASSERT_FALSE(macro->GetMacro({"m2"})) << macro->Dump();

    //@todo Bug: https://github.com/rsashka/newlang/issues/22

    //    ASSERT_NO_THROW(
    //            ASSERT_TRUE(Parse("dsl", macro));
    //            );
    //
    //
    //    ASSERT_EQ(6, macro->GetCount());
    //    ASSERT_TRUE(macro->GetMacro({"m1"})) << macro->Dump();
    //    ASSERT_TRUE(macro->GetMacro({"m2"})) << macro->Dump();

    // TEST_F(NamedTest, Multiple) {
    //     MacroBuffer macro;
    //     ASSERT_EQ(0, macro->GetCount());
    //
    //     ASSERT_NO_THROW(Parse("@@alias@@ replace @@@@", macro));
    //     ASSERT_EQ(1, macro->GetCount()) << macro->Dump();
    //     ASSERT_NO_THROW(Parse("@@alias second(...)@@ second(@$#, @$*) @@@@", macro));
    //     ASSERT_EQ(2, macro->GetCount()) << macro->Dump();
    //     ASSERT_NO_THROW(Parse("@@text(...)@@ @@@ text1(@$#, @$*);\n text1 @@@@", macro));
    //     ASSERT_EQ(3, macro->GetCount()) << macro->Dump();
    //     ASSERT_NO_THROW(Parse("@@dsl()@@ @@@  @@m1@@ mm@@;\n  @@m2@@ := @@mm@@;\n @ @@@@", macro));
    //
    //     ASSERT_EQ(4, macro->GetCount()) << macro->Dump();
    //     ASSERT_TRUE(macro->GetMacro({"alias"}));
    //     TermPtr macro_alias = macro->GetMacro({"alias"});
    //     ASSERT_TRUE(macro->GetMacro({"alias", "second"}));
    //     TermPtr macro_second = macro->GetMacro({"alias", "second"});
    //     ASSERT_TRUE(macro->GetMacro({"text"}));
    //     TermPtr macro_text = macro->GetMacro({"text"});
    //     ASSERT_TRUE(macro->GetMacro({"dsl"}));
    //     TermPtr macro_dsl = macro->GetMacro({"dsl"});
    //
    //
    //     TermPtr term = Term::Create(parser::token_type::NAME, TermID::NAME, "alias");
    //
    //     ASSERT_TRUE(macro->GetMacro({"alias"}));
    //     ASSERT_TRUE(macro->GetMacro({"alias", "second"}));
    //
    //
    //     SequenceType buff; //
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_alias)); // alias
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_second)); // alias second(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_text)); // text(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_dsl)); // dsl()
    //
    //
    //     buff.push_back(term); // alias
    //
    //     ASSERT_TRUE(MacroBuffer::IdentityMacro(buff, macro_alias)); // alias
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_second)); // alias second(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_text)); // text(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_dsl)); // dsl()
    //
    //     buff.push_back(term); // alias alias
    //
    //     ASSERT_TRUE(MacroBuffer::IdentityMacro(buff, macro_alias)); // alias
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_second)); // alias second(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_text)); // text(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_dsl)); // dsl()
    //
    //     buff.push_back(Term::Create(parser::token_type::NAME, TermID::NAME, "second")); // alias alias second
    //
    //     ASSERT_TRUE(MacroBuffer::IdentityMacro(buff, macro_alias)); // alias
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_second)); // alias second(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_text)); // text(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_dsl)); // dsl()
    //
    //     buff.push_back(Term::Create(parser::token_type::LPAREN, TermID::LPAREN, "(")); // alias alias second (
    //
    //     ASSERT_TRUE(MacroBuffer::IdentityMacro(buff, macro_alias)); // alias replace(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_second)); // alias second(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_text)); // text(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_dsl)); // dsl()
    //
    //     buff.push_back(Term::Create(parser::token_type::NAME, TermID::NAME, "arg")); // alias alias second ( arg
    //
    //     ASSERT_TRUE(MacroBuffer::IdentityMacro(buff, macro_alias)); // alias replace(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_second)); // alias second(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_text)); // text(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_dsl)); // dsl()
    //
    //     buff.push_back(Term::Create(parser::token_type::RPAREN, TermID::RPAREN, ")")); // alias alias second ( arg )
    //
    //     ASSERT_TRUE(MacroBuffer::IdentityMacro(buff, macro_alias)); // alias replace(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_second)); // alias second(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_text)); // text(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_dsl)); // dsl()
    //
    //     buff.erase(buff.begin()); // alias second ( arg )
    //
    //     ASSERT_TRUE(MacroBuffer::IdentityMacro(buff, macro_alias)); // alias replace
    //     ASSERT_TRUE(MacroBuffer::IdentityMacro(buff, macro_second)); // alias second(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_text)); // text(...)
    //     ASSERT_FALSE(MacroBuffer::IdentityMacro(buff, macro_dsl)); // dsl()
    //
    //
    //     ASSERT_TRUE(Parse("@alias", macro));
    //     ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    //     ASSERT_STREQ("replace", ast->toString().c_str());
    //
    //     ASSERT_TRUE(Parse("alias", macro));
    //     ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    //     ASSERT_EQ("replace", ast->getText());
    //
    //     ASSERT_TRUE(Parse("alias second", macro));
    //     ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    //     ASSERT_EQ("second", ast->getText());
    //
    //
    //
    //     ASSERT_EQ(4, macro->GetCount());
    //     ASSERT_FALSE(macro->GetMacro({"m1"})) << macro->Dump();
    //     ASSERT_FALSE(macro->GetMacro({"m2"})) << macro->Dump();
    //
    //     //@todo Bug: https://github.com/rsashka/newlang/issues/22
    //     //    ASSERT_TRUE(Parse("dsl", macro));
    //     //
    //     //    ASSERT_EQ(6, macro->GetCount());
    //     //    ASSERT_TRUE(macro->GetMacro({"m1"})) << macro->Dump();
    //     //    ASSERT_TRUE(macro->GetMacro({"m2"})) << macro->Dump();
    //
}

TEST_F(MacroTest, MacroAlias) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_TRUE(macro->isEmpty());

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@ @@@@;"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@  @@@@;"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@  macro @@@@  @@;"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@  @@@@  macro  @@;"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@  @abc  @@"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@  @$macro  @@"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);

    ASSERT_TRUE(Parse("@@ alias @@@@;", macro));
    ASSERT_EQ(0, macro->CountInScope(0));
    ASSERT_TRUE(Parse("@@ alias $alias2 @@@@;", macro));
    ASSERT_EQ(0, macro->CountInScope(0));

    ASSERT_TRUE(Parse("@@alias@@ replace @@@@", macro));
    ASSERT_TRUE(Parse("@@alias2@@ alias @@@@", macro));
    ASSERT_TRUE(Parse("@@fail@@ fail @@@@", macro));

    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias"}));
    TermPtr macro_alias = macro->GetMacro({"alias"});
    ASSERT_TRUE(macro_alias);
    ASSERT_EQ(TermID::MACRO_SEQ, macro_alias->getTermID()) << toString(macro_alias->getTermID());
    ASSERT_TRUE(macro_alias->m_left);
    ASSERT_EQ(TermID::MACRO_SEQ, macro_alias->m_left->getTermID()) << toString(macro_alias->m_left->getTermID());
    ASSERT_TRUE(macro_alias->m_right);
    ASSERT_TRUE(macro_alias->m_right->m_sequence.size()) << macro_alias->m_right->toString();
    ASSERT_EQ("replace", macro_alias->m_right->m_sequence[0]->getText());

    ASSERT_TRUE(macro->GetMacro({"alias2"})) << macro->Dump();
    TermPtr macro_alias2 = macro->GetMacro({"alias2"});
    ASSERT_TRUE(macro_alias2);
    ASSERT_TRUE(macro_alias2->isMacro());
    ASSERT_EQ(TermID::MACRO_SEQ, macro_alias2->getTermID()) << toString(macro_alias2->getTermID());
    ASSERT_TRUE(macro_alias2->m_left);
    ASSERT_EQ(TermID::MACRO_SEQ, macro_alias2->m_left->getTermID()) << toString(macro_alias2->m_left->getTermID());
    ASSERT_EQ("alias", macro_alias2->m_right->m_sequence[0]->getText());

    ASSERT_TRUE(macro->GetMacro({"fail"})) << macro->Dump();
    TermPtr macro_fail = macro->GetMacro({"fail"});
    ASSERT_TRUE(macro_fail);
    ASSERT_TRUE(macro_fail->m_left);
    ASSERT_EQ(TermID::MACRO_SEQ, macro_fail->m_left->getTermID());
    ASSERT_EQ("fail", macro_fail->m_right->m_sequence[0]->getText());

    TermPtr term = Term::Create(TermID::NAME, "alias", {}, parser::token_type::NAME);

    ASSERT_TRUE(macro->FindMacroList(term->getText()));

    SequenceType vals = *macro->FindMacroList(term->getText());
    ASSERT_EQ(1, vals.size());

    SequenceType buff;
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_alias));
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_alias2));
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_fail));

    buff.push_back(term);

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_alias2));
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_fail));

    term = Term::Create(TermID::NAME, "alias", {}, parser::token_type::NAME);
    buff.push_back(term);

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_alias2));
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_fail));

    MacroArgsType macro_args;

    ASSERT_EQ(1, macro->ExtractArgs(buff, macro_alias, macro_args));
    ASSERT_EQ(3, macro_args.size()) << macro->Dump(macro_args);

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_fail, macro_args));

    // macro_alias has m_right of type MACRO_SEQ, ExpandString requires MACRO_STR
    // Use ExpandMacros instead
    Parser parser(m_ctx, &m_postlex);
    SequenceType block;
    block = macro->ExpandMacros(macro_alias, macro_args, parser, MapperRange{});
    ASSERT_EQ(1, block.size());
    ASSERT_TRUE(block[0]);
    ASSERT_EQ("replace", block[0]->getText());

    ASSERT_EQ(3, macro->CountInScope(0));

    ASSERT_TRUE(Parse("alias", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("replace", ast->toString());

    ASSERT_TRUE(Parse("alias2", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("replace", ast->toString());

    //    ASSERT_ANY_THROW(Parse("fail", macro));
}

TEST_F(MacroTest, MacroArgs) {

    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    SequenceType buffer;
    // Тест фокусируется на аргументах/раскрытии; операторная семантика `:=`/`::=`/`=` убрана,
    // повторные определения имён — молча (последнее определение побеждает).
    ASSERT_NO_THROW(Parse("@__OPTION_PUSH__; @__OPTION__(\"macro-redefined\", \"ignore\");", macro));

    SequenceType vect;
    TermPtr macro_alias1;
    //
    //    ASSERT_TRUE(Parse("@@alias@@replace1@@;@@alias2@@replace2@@", macro));
    //    ASSERT_EQ(2, macro->GetCount());
    //
    //    iter = macro.map::find("alias");
    //    ASSERT_TRUE(iter != macro.end());
    //
    // vect = iter->second;
    //
    //    ASSERT_EQ(1, vect.size()) << macro->Dump();
    //
    //    macro_alias1 = vect[0].macro;
    //    ASSERT_TRUE(macro_alias1);
    //    ASSERT_EQ("alias", macro_alias1->getText());
    //    ASSERT_FALSE(macro_alias1->isCall()) << macro_alias1->toString().c_str();
    //    ASSERT_TRUE(macro_alias1->getTermID() == TermID::MACRO_DEF) << macro_alias1->toString().c_str();
    //    ASSERT_TRUE(macro_alias1->m_right);
    //    ASSERT_EQ(1, macro_alias1->m_right->m_sequence.size());
    //    ASSERT_STREQ("replace1", macro_alias1->m_right->m_sequence[0]->getText());

    ASSERT_EQ(0, macro->CountInScope(0));

    ASSERT_NO_THROW(Parse("@@alias@@ replace1 @@@@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@alias@@ replace2 @@@@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_NO_THROW(Parse("@@alias@@ replace3 @@@@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@alias(arg)@@ replace2(@$arg) @@@@", macro)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@ alias @@@@;", macro)) << macro->Dump();
    ASSERT_EQ(0, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@alias(arg, ... )@@ replace2(@$arg) @@@@", macro)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias"}));
    ASSERT_EQ("@@ alias ( arg , ... ) @@", macro->GetMacro({"alias"})->toString());

    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_NO_THROW(Parse("@@alias(arg, ... )@@ replace3(@$arg) @@@@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@alias(arg, ... )@@ replace4(@$arg) @@@@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias"}));
    ASSERT_EQ("@@ alias ( arg , ... ) @@", macro->GetMacro({"alias"})->toString());

    ASSERT_NO_THROW(Parse("@@alias3(...)@@ replace3(@$#, @$...) @@@@", macro)) << macro->Dump();
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();

    //    ASSERT_NO_THROW(Parse("@@alias(arg) second@@ ::= @@replace3(@$*)@@;", macro)) << macro->Dump();
    //    TermPtr test = macro->GetMacro({"alias", "second"});
    //    ASSERT_TRUE(test) << macro->Dump();
    //    ASSERT_EQ(TermID::MACRO_SEQ, test->getTermID()) << test->toString().c_str();
    //    ASSERT_EQ(2, macro->GetCount()) << macro->Dump();
    //
    //    std::vector<std::string> id = MacroBuffer::GetMacroId(test);
    //    ASSERT_EQ(2, id.size()) << test->toString().c_str();
    //    ASSERT_STREQ("alias", id[0].c_str());
    //    ASSERT_STREQ("second", id[1].c_str());

    ASSERT_NO_THROW(Parse("@@macro(arg, ... )@@@ 3*@$arg @@@@", macro)) << macro->Dump();
    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();

    SequenceType* alias_list = macro->FindMacroList("alias");
    ASSERT_TRUE(alias_list);

    vect = *alias_list;

    ASSERT_EQ(1, vect.size());

    macro_alias1 = vect[0];
    ASSERT_TRUE(macro_alias1);
    ASSERT_EQ("@@ alias ( arg , ... ) @@", macro_alias1->toString());
    ASSERT_TRUE(macro_alias1->m_right);
    ASSERT_EQ(4, macro_alias1->m_right->m_sequence.size());
    ASSERT_EQ("replace4", macro_alias1->m_right->m_sequence[0]->getText());

    //    TermPtr macro_alias2 = vect[1];
    //    ASSERT_TRUE(macro_alias2);
    //    ASSERT_EQ("alias", macro_alias2->getText());
    //    ASSERT_TRUE(macro_alias2->getTermID() == TermID::MACRO_SEQ) << macro_alias2->toString().c_str();
    //    ASSERT_EQ(4, macro_alias2->m_sequence.size());
    //    ASSERT_TRUE(macro_alias2->m_right);
    //    ASSERT_EQ(4, macro_alias2->m_right->m_sequence.size()) << macro_alias2->m_right->m_sequence[0]->getText();
    //    ASSERT_STREQ("replace2", macro_alias2->m_right->m_sequence[0]->getText());
    //    ASSERT_STREQ("(", macro_alias2->m_right->m_sequence[1]->getText());
    //    ASSERT_STREQ("@$arg", macro_alias2->m_right->m_sequence[2]->getText());
    //    ASSERT_STREQ(")", macro_alias2->m_right->m_sequence[3]->getText());

    //    TermPtr macro_alias3 = vect[2];
    //    ASSERT_TRUE(macro_alias3);
    //    ASSERT_EQ("alias", macro_alias3->getText());
    //    ASSERT_TRUE(macro_alias3->getTermID() == TermID::MACRO_SEQ) << macro_alias3->toString().c_str();
    //    ASSERT_EQ(5, macro_alias3->m_sequence.size());
    //    ASSERT_STREQ("(", macro_alias3->m_sequence[1]->getText());
    //    ASSERT_TRUE(macro_alias3->m_right);
    //    ASSERT_EQ(4, macro_alias3->m_right->m_sequence.size());
    //    ASSERT_EQ("replace3", macro_alias3->m_right->m_sequence[0]->getText());
    //    ASSERT_STREQ("(", macro_alias3->m_right->m_sequence[1]->getText());
    //    ASSERT_STREQ("@$*", macro_alias3->m_right->m_sequence[2]->getText());
    //    ASSERT_STREQ(")", macro_alias3->m_right->m_sequence[3]->getText());

    //    ASSERT_EQ(macro_alias1.get(), macro->GetMacro({"alias"}).get()) << macro->Dump();
    //    //    ASSERT_EQ(macro_alias2.get(), macro->GetMacro({"alias", "second"}).get()) << macro->Dump();
    //    //    ASSERT_EQ(macro_alias3.get(), macro->GetMacro({"alias", "(", "$", ")", "second"}).get()) << macro->Dump();
    //
    //
    SequenceType* macro_list = macro->FindMacroList("macro");
    ASSERT_TRUE(macro_list);

    vect = *macro_list;
    ASSERT_EQ(1, vect.size());
    TermPtr macro_macro1 = vect[0];
    ASSERT_TRUE(macro_macro1);
    ASSERT_EQ("@@ macro ( arg , ... ) @@", macro_macro1->toString());
    ASSERT_EQ(macro_macro1.get(), macro->GetMacro({"macro"}).get()); // Поиск по MacroID и возврат TermPtr
    ASSERT_TRUE(macro_macro1->m_right);
    ASSERT_TRUE(macro_macro1->m_right->getTermID() == TermID::MACRO_STR) << macro_macro1->toString().c_str();
    //

    SequenceType buff;
    MacroArgsType macro_args;

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_alias1, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::NAME, "alias", {}, parser::token_type::NAME));

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_alias1, macro_args)) << macro_alias1->toString().c_str();

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args)) << macro_macro1->toString().c_str();

    buff.push_back(Term::Create(TermID::LPAREN, "(", {}, parser::token_type::LPAREN));

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_alias1, macro_args));

    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN));

    size_t count;
    ASSERT_NO_THROW(count = macro->ExtractArgs(buff, macro_alias1, macro_args));
    ASSERT_EQ(3, count);

    buff.erase(buff.end());

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::NAME, "value", {}, parser::token_type::NAME));

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_alias1, macro_args));

    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN));

    ASSERT_EQ(4, buff.size());
    ASSERT_NO_THROW(count = macro->ExtractArgs(buff, macro_alias1, macro_args)) << macro->Dump(buff);
    ASSERT_EQ(4, count);
    buff.erase(buff.end());

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::COMMA, ",", {}, parser::token_type::COMMA));

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::NAME, "value2", {}, parser::token_type::NAME));

    buff.push_back(Term::Create(TermID::NAME, "value3", {}, parser::token_type::NAME));

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN));

    ASSERT_NO_THROW(ASSERT_EQ(7, macro->ExtractArgs(buff, macro_alias1, macro_args)););
    ASSERT_EQ(6, macro_args.size()) << macro->Dump(macro_args);

    //        ASSERT_EQ(7, MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //        ASSERT_EQ(4, macro_args.size()) << MacroBuffer::Dump(macro_args);

    ASSERT_EQ(7, buff.size());
    ASSERT_NO_THROW(ASSERT_EQ(7, macro->ExtractArgs(buff, macro_macro1, macro_args)););
    ASSERT_EQ(6, macro_args.size()) << macro->Dump(macro_args);

    //        ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));

    buff.push_back(Term::Create(TermID::SEMICOLON, ";", {}, parser::token_type::SEMICOLON));

    ASSERT_NO_THROW(ASSERT_EQ(7, macro->ExtractArgs(buff, macro_alias1, macro_args)););

    ASSERT_EQ(6, macro_args.size()) << macro->Dump(macro_args);

    ASSERT_EQ(8, buff.size()) << macro->Dump(buff);
    ASSERT_NO_THROW(ASSERT_EQ(7, macro->ExtractArgs(buff, macro_macro1, macro_args)););
    ASSERT_EQ(6, macro_args.size()) << macro->Dump(macro_args);

    //    buff.push_back(Term::Create(parser::token_type::NAME, TermID::NAME, "last_term"));
    //
    //    ASSERT_EQ(1, MacroBuffer::ExtractArgs(buff, macro_alias1, macro_args));
    //    ASSERT_EQ(0, macro_args.size()) << MacroBuffer::Dump(macro_args);
    //
    //    SequenceType res = MacroBuffer::ExpandMacros(macro_alias1, macro_args);
    //    ASSERT_EQ(1, res.size());
    //    ASSERT_STREQ("replace1", res[0]->getText());

    //        ASSERT_EQ(7, MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //        ASSERT_EQ(4, macro_args.size()) << MacroBuffer::Dump(macro_args);
    //
    //        res = MacroBuffer::ExpandMacros(macro_alias2, macro_args);
    //        ASSERT_EQ(4, res.size());
    //        ASSERT_STREQ("replace2", res[0]->getText());
    //        ASSERT_STREQ("(", res[1]->getText());
    //        ASSERT_STREQ("value", res[2]->getText()) << MacroBuffer::Dump(macro_args);
    //        ASSERT_STREQ(")", res[3]->getText());
    //
    //        // Нет анализаи на соотеветстви макроса, только извлечение значений шаблона
    //        ASSERT_EQ(8, MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args)) << MacroBuffer::Dump(macro_args);
    //        ASSERT_EQ(4, macro_args.size()) << MacroBuffer::Dump(macro_args);

    //        res = MacroBuffer::ExpandMacros(macro_alias3, macro_args);
    //        ASSERT_EQ(7, res.size());
    //        ASSERT_STREQ("replace3", res[0]->getText());
    //        ASSERT_STREQ("(", res[1]->getText());
    //        ASSERT_STREQ("value", res[2]->getText()) << MacroBuffer::Dump(macro_args);
    //        ASSERT_STREQ(",", res[3]->getText()) << MacroBuffer::Dump(macro_args);
    //        ASSERT_STREQ("value2", res[4]->getText()) << MacroBuffer::Dump(macro_args);
    //        ASSERT_STREQ("value3", res[5]->getText()) << MacroBuffer::Dump(macro_args);
    //        ASSERT_STREQ(")", res[6]->getText());

    buff.clear();
    buff.push_back(Term::Create(TermID::NAME, "macro", {}, parser::token_type::NAME));
    buff.push_back(Term::Create(TermID::LPAREN, "(", {}, parser::token_type::LPAREN));
    buff.push_back(Term::Create(TermID::NUMBER, "5", {}, parser::token_type::NUMBER));
    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN));
    buff.push_back(Term::Create(TermID::SEMICOLON, ";", {}, parser::token_type::SEMICOLON));

    TermPtr macro_macro = macro->GetMacro({"macro"});
    ASSERT_TRUE(macro_macro);

    ASSERT_NO_THROW(ASSERT_EQ(4, macro->ExtractArgs(buff, macro_macro, macro_args)) << macro->Dump(macro_args););
    ASSERT_EQ(5, macro_args.size()) << macro->Dump(macro_args);

    std::string str = macro->ExpandString(macro_macro, macro_args);
    ASSERT_STREQ(" 3*5  ", str.c_str());

    buff.clear();
    buff.push_back(Term::Create(TermID::NAME, "alias3", {}, parser::token_type::NAME));
    buff.push_back(Term::Create(TermID::LPAREN, "(", {}, parser::token_type::LPAREN));
    buff.push_back(Term::Create(TermID::NUMBER, "5", {}, parser::token_type::NUMBER));
    buff.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN));
    buff.push_back(Term::Create(TermID::SEMICOLON, ";", {}, parser::token_type::SEMICOLON));

    TermPtr macro_alias3 = macro->GetMacro({"alias3"});
    ASSERT_TRUE(macro_alias3);

    ASSERT_EQ("@@ alias3 ( ... ) @@", macro_alias3->toString());
    ASSERT_TRUE(macro_alias3->m_right);
    ASSERT_EQ(6, macro_alias3->m_right->m_sequence.size());
    ASSERT_EQ("replace3", macro_alias3->m_right->m_sequence[0]->getText());

    ASSERT_NO_THROW(ASSERT_EQ(4, macro->ExtractArgs(buff, macro_alias3, macro_args)) << macro->Dump(macro_args););
    ASSERT_EQ(4, macro_args.size()) << macro->Dump(macro_args);

    auto iter_arg = macro_args.begin();
    ASSERT_TRUE(iter_arg != macro_args.end()) << macro->Dump(macro_args);

    ASSERT_STREQ("@$#", iter_arg->first.c_str());
    ASSERT_EQ(1, iter_arg->second.size());
    ASSERT_STREQ("1", std::string(iter_arg->second.at(0)->getText()).c_str()) << macro->Dump(macro_args);

    iter_arg++;
    ASSERT_TRUE(iter_arg != macro_args.end());

    ASSERT_STREQ("@$*", iter_arg->first.c_str());
    ASSERT_STREQ("( 5 , )", macro->Dump(iter_arg->second).c_str());

    iter_arg++;
    ASSERT_TRUE(iter_arg != macro_args.end());

    ASSERT_STREQ("@$...", iter_arg->first.c_str());
    ASSERT_EQ(1, iter_arg->second.size());

    iter_arg++;
    ASSERT_TRUE(iter_arg != macro_args.end());

    ASSERT_STREQ("@$1", iter_arg->first.c_str());
    ASSERT_EQ(1, iter_arg->second.size());

    iter_arg++;
    ASSERT_TRUE(iter_arg == macro_args.end());

    //    ASSERT_EQ(1, macro_args[1].size());
    //    ASSERT_STREQ("@$...", (macro_args.begin() + 1)->first.c_str());

    //    ASSERT_EQ(1, macro_args[0].size());
    //    ASSERT_STREQ("@$1", macro_args[0][0]->name(0).c_str());
    //
    //    ASSERT_EQ(1, macro_args[1].size());
    //    ASSERT_STREQ("@$1", macro_args[1][0]->name(0).c_str());
    //    ASSERT_EQ(1, macro_args[1].size());
    //
    //    ASSERT_EQ(1, macro_args[2].size());
    //    ASSERT_STREQ("@$#", macro_args[2][0]->name(0).c_str());
    //    ASSERT_EQ(1, macro_args[2].size());
    //
    //    ASSERT_EQ(1, macro_args[3].size());
    //    ASSERT_STREQ("@$*", macro_args[3][0]->name(0).c_str());
    //    ASSERT_EQ(1, macro_args[3].size());

    // alias3(5) -> replace3(@$#, @$*) т.е replace3(1,5)
    Parser parser(m_ctx, &m_postlex);
    SequenceType blk = macro->ExpandMacros(macro_alias3, macro_args, parser, MapperRange{});
    ASSERT_EQ(6, blk.size()) << macro->Dump(blk).c_str();
    ASSERT_EQ("replace3", blk[0]->getText()) << macro_alias3->m_right->toString();
    ASSERT_EQ("(", blk[1]->getText()) << macro_alias3->m_right->toString();
    ASSERT_EQ("1", blk[2]->getText()) << macro_alias3->m_right->toString();
    ASSERT_EQ(",", blk[3]->getText()) << macro_alias3->m_right->toString();
    ASSERT_EQ("5", blk[4]->getText()) << macro_alias3->m_right->toString();
    ASSERT_EQ(")", blk[5]->getText()) << macro_alias3->m_right->toString();

    //    body = "@macro(11, ...)";
    //    args = Parser::ParseMacroArgs(body);
    //    ASSERT_EQ(2, args.size());
    //    ASSERT_STREQ("11", args[0].c_str());
    //    ASSERT_STREQ("...", args[1].c_str());
    //
    //    body = "@return(...)    --@$*--";
    //    args = Parser::ParseMacroArgs(body);
    //    ASSERT_EQ(1, args.size());
    //    ASSERT_STREQ("...", args[0].c_str());
    //
    //    ASSERT_ANY_THROW(
    //            body = "@macro(,)";
    //            args = Parser::ParseMacroArgs(body);
    //            );
    //    ASSERT_ANY_THROW(
    //            body = "@macro( , )";
    //            args = Parser::ParseMacroArgs(body);
    //            );
    //    ASSERT_ANY_THROW(
    //            body = "@macro(,,)";
    //            args = Parser::ParseMacroArgs(body);
    //            );
    //
    //    body = "@macro)";
    //    args = Parser::ParseMacroArgs(body);
    //    ASSERT_EQ(0, args.size());
    //
    //    body = "@macro\n";
    //    args = Parser::ParseMacroArgs(body);
    //    ASSERT_EQ(0, args.size());
    //
    //    body = "@macro)";
    //    args = Parser::ParseMacroArgs(body);
    //    ASSERT_EQ(0, args.size());
    //
    //    body = "@@macro()";
    //    args = Parser::ParseMacroArgs(body);
    //    ASSERT_EQ(0, args.size());
    //
    //    body = "macro";
    //    args = Parser::ParseMacroArgs(body);
    //    ASSERT_EQ(0, args.size());
    //
    //    body = "";
    //    args = Parser::ParseMacroArgs(body);
    //    ASSERT_EQ(0, args.size());
}

TEST_F(MacroTest, MacroArityGrouping) {
    // Один и тот же первый терм имени = группа; разные арности сосуществуют БЕЗ «duplication».
    // Дубликат диагностируется только при полном совпадении сигнатуры (всех термов).
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_TRUE(macro->isEmpty());

    ASSERT_NO_THROW(Parse("@@ break @@ ++ @@@@;", macro)) << macro->Dump();
    ASSERT_NO_THROW(Parse("@@ break $label @@ @$label ++ @@@@;", macro)) << macro->Dump();
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();

    // Переопределение ТОЙ ЖЕ сигнатуры (break) - по умолчанию предупреждение.
    ASSERT_NO_THROW(Parse("@@ break @@ ++ @@@@;", macro)) << macro->Dump();
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();

    // Шаблоны той же арности и структуры (break $label / break $x) - одна сигнатура.

    // Longest-match: `break;` → arity-1; `break outer;` → arity-2.
    ASSERT_NO_THROW(Parse("break;", macro)) << macro->Dump();
    ASSERT_STREQ("++ ;", LexOut().c_str());
    ASSERT_NO_THROW(Parse("break outer;", macro)) << macro->Dump();
    ASSERT_STREQ("outer ++ ;", LexOut().c_str());

    // Void vs value: шаблон как последний терм сигнатуры НЕ матчит `;`.
    // `ret;` → void-макрос (arity-1), а НЕ varargs с пустым аргументом.
    ASSERT_NO_THROW(Parse("@@ ret @@ :: ++ @@@@;", macro)) << macro->Dump();
    ASSERT_NO_THROW(Parse("@@ ret $... @@ :: ++ @$... ++ @@@@;", macro)) << macro->Dump();
    ASSERT_NO_THROW(Parse("ret;", macro)) << macro->Dump();
    ASSERT_STREQ(":: ++ ;", LexOut().c_str());
    ASSERT_NO_THROW(Parse("ret 42;", macro)) << macro->Dump();
    ASSERT_STREQ(":: ++ 42 ++ ;", LexOut().c_str());
}

TEST_F(MacroTest, MacroCheck) {

    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    SequenceType buffer;

    ASSERT_TRUE(macro->isEmpty());

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@testargs(arg)@@ @$bad_arg @@@@", macro)) << macro->Dump();
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@testargs(arg)@@ @$... @@@@", macro)) << macro->Dump();
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@testargs(arg, ...)@@ @$2 @@@@", macro)) << macro->Dump();
    EXPECT_GT(m_ctx.diag().errorCount(), 0);

    ASSERT_NO_THROW(Parse("@@ macro2(...) @@ replace2( @$#, @$... ,@$* ) @@@@", macro)) << macro->Dump();
    ASSERT_NO_THROW(Parse("macro2(1,9)", macro)) << macro->Dump() << LexOut().c_str();
    ASSERT_STREQ("replace2 ( 2 , 1 , 9 , ( 1 , 9 , ) )", LexOut().c_str());

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@ return $... $... @@ @$... @@@@", macro)) << macro->Dump();
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    // Операторный `$...` + call-группа (пустой `()`) - оператор поглощает вызов: ошибка.
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@ return() $... @@ @$... @@@@", macro)) << macro->Dump();
    EXPECT_GT(m_ctx.diag().errorCount(), 0);

    ASSERT_NO_THROW(Parse("@@ return $... @@ :: ++ @$... ++ @@@@", macro)) << macro->Dump();
    ASSERT_NO_THROW(Parse("return (1, 2, 3,)", macro)) << macro->Dump() << " ------  " << LexOut().c_str();
    ASSERT_STREQ(":: ++ ( 1 , 2 , 3 , ) ++", LexOut().c_str());
    // TEST_F(NamedTest, MacroExpand) {
    //
    //     std::string macro = "@macro 12345";
    //     std::string body = "@macro";
    //     std::string result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("12345", result.c_str());
    //
    //     body = "@macro @macro";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("12345 12345", result.c_str());
    //
    //     body = "@macro @macro @macro";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("12345 12345 12345", result.c_str());
    //
    //     macro = "@macro() 12345";
    //     body = "@macro @macro @macro";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("@macro @macro @macro", result.c_str());
    //
    //     macro = "@macro()12345";
    //     body = "@macro() @macro() @macro";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("12345 12345 @macro", result.c_str());
    //
    //     macro = "@macro()12345";
    //     body = "@macro(88) @macro(99) @macro";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("12345 12345 @macro", result.c_str());
    //
    //
    //     macro = "@macro(arg)@$arg";
    //     body = "@macro(88)";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("88", result.c_str());
    //
    //     macro = "@macro(arg)no arg @$arg";
    //     body = "@macro(99)";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("no arg 99", result.c_str());
    //
    //     macro = "@macro(arg)  no arg @$arg no arg";
    //     body = "@macro(88) @macro(99)";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("  no arg 88 no arg   no arg 99 no arg", result.c_str());
    //
    //     macro = "@macro(arg1,arg2)  @$arg1 arg @$arg2 @$arg2";
    //     body = "@macro(88,99)";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("  88 arg 99 99", result.c_str());
    //
    //     macro = "@macro(arg1,arg2)  @$arg1 @$arg2 @$arg2";
    //     body = "@macro(1,2) @macro(3,44)";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("  1 2 2   3 44 44", result.c_str());
    //
    //     macro = "@macro(arg1,arg2)  @$1 @$2 @$1";
    //     body = "@macro(1,2) @macro(3,44)";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("  1 2 1   3 44 3", result.c_str());
    //
    //     macro = "@macro(arg1,arg2)@$*";
    //     body = "@macro(1,2)";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("1,2", result.c_str());
    //
    //     macro = "@macro(arg1,arg2)@$* @$1 @$arg2@$*";
    //     body = "@macro(1,2)";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("1,2 1 21,2", result.c_str());
    //
    //     macro = "@macro(arg1,arg2)@$* @$1 @$arg2@$*";
    //     body = "@macro(1,2)@macro(1,2)";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("1,2 1 21,21,2 1 21,2", result.c_str());
    //
    //     macro = "@@return    --@@@@";
    //     body = "@return(100);";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("@return(100);", result.c_str());
    //
    //     macro = "@return(...)--@$*--";
    //     body = "@return(100);";
    //     result = MacroBuffer::ExpandMacro(macro, body);
    //     ASSERT_STREQ("--100--;", result.c_str());
    //
    // }

    // TEST_F(NamedTest, MacroDSL) {
    //
    //     Parser::MacrosStore macros;
    //     std::string dsl = ""
    //             "@if(cond)@@      [$cond]-->@@"
    //             "@elseif(cond)@@ ,[$cond]-->@@"
    //             "@else@@         ,[...]-->@@"
    //             ""
    //             "@while(cond)@@  [$cond]<->@@"
    //             "@dowhile(cond)@@<->[$cond]@@"
    //             "@return@         --@"
    //             "@return(...)@    --$...--@"
    //             "@dowhile(cond)@@@@"
    //             "@@@@"
    //             "";
    //
    //     while(Parser::ExtractMacros(dsl, macros))
    //         ;
    //     ASSERT_EQ(7, macros.size());
    //
    //
    //
}

TEST_F(MacroTest, MacroTest) {

    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    SequenceType buffer;

    ASSERT_TRUE(macro->isEmpty());

    ASSERT_NO_THROW(Parse("@@alias@@ replace @@@@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_EQ(1, macro->FindMacroList("alias")->size()) << macro->Dump();
    ASSERT_TRUE(macro->FindMacroList("alias")->at(0)) << macro->Dump();
    ASSERT_STREQ("alias", macro->toMacroHash(macro->FindMacroList("alias")->at(0)).c_str()) << macro->FindMacroList("alias")->at(0)->toString();
    ASSERT_EQ(1, macro->GetMacroId(macro->FindMacroList("alias")->at(0)).size());
    ASSERT_TRUE(macro->GetMacro({"alias"}));

    ASSERT_NO_THROW(Parse("alias", macro)) << macro->Dump();
    ASSERT_STREQ("replace", LexOut().c_str()) << macro->Dump();

    ASSERT_NO_THROW(Parse("alias()", macro)) << macro->Dump();
    ASSERT_STREQ("replace ( )", LexOut().c_str()) << macro->Dump();

    ASSERT_NO_THROW(Parse("alias(...)", macro)) << macro->Dump();
    ASSERT_STREQ("replace ( ... )", LexOut().c_str()) << macro->Dump();

    ASSERT_NO_THROW(Parse("alias(1,2,3)", macro)) << macro->Dump();
    ASSERT_STREQ("replace ( 1 , 2 , 3 )", LexOut().c_str()) << macro->Dump();

    ASSERT_NO_THROW(Parse("@alias", macro)) << macro->Dump();
    ASSERT_STREQ("replace", LexOut().c_str()) << macro->Dump();

    ASSERT_NO_THROW(Parse("@alias(); @alias", macro)) << macro->Dump() << " LEX: \"" << LexOut().c_str() << "\"";
    ASSERT_STREQ("replace ( ) ; replace", LexOut().c_str()) << macro->Dump();

    ASSERT_NO_THROW(Parse("@alias(...)", macro)) << macro->Dump();
    ASSERT_STREQ("replace ( ... )", LexOut().c_str()) << macro->Dump();

    ASSERT_NO_THROW(Parse("@alias(1,2,3); none", macro)) << macro->Dump();
    ASSERT_STREQ("replace ( 1 , 2 , 3 ) ; none", LexOut().c_str()) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@ macro1 @@ replace1 @@@@", macro)) << macro->Dump();
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"macro1"}));
    ASSERT_TRUE(macro->GetMacro({"macro1"})->isMacro());

    ASSERT_NO_THROW(Parse("macro1", macro)) << macro->Dump();
    ASSERT_STREQ("replace1", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@macro1()", macro)) << macro->Dump();
    ASSERT_STREQ("replace1 ( )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@macro1(...)", macro)) << macro->Dump();
    ASSERT_STREQ("replace1 ( ... )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@macro1(1,2,3)", macro)) << macro->Dump();
    ASSERT_STREQ("replace1 ( 1 , 2 , 3 )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@macro1", macro)) << macro->Dump();
    ASSERT_STREQ("replace1", LexOut().c_str());

    // Макрос macro1 определн без скобок, а тут скобки есть
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@macro1() @alias", macro)) << macro->Dump();
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("none @macro1(...)", macro)) << macro->Dump();
    EXPECT_GT(m_ctx.diag().errorCount(), 0);

    ASSERT_NO_THROW(Parse("@@ macro2(...) @@ replace2( @$... ) @@@@", macro)) << macro->Dump();
    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"macro2"}));
    ASSERT_TRUE(macro->GetMacro({"macro2"})->m_left);
    ASSERT_EQ(TermID::MACRO_SEQ, macro->GetMacro({"macro2"})->m_left->getTermID());

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("macro2", macro)) << macro->Dump();
    EXPECT_GT(m_ctx.diag().errorCount(), 0);

    ASSERT_NO_THROW(Parse("macro2()", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("macro2()", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("macro2( 1 )", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( 1 )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("macro2(1,2,3)", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( 1 , 2 , 3 )", LexOut().c_str());

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@macro2", macro)) << macro->Dump();
    EXPECT_GT(m_ctx.diag().errorCount(), 0);

    ASSERT_NO_THROW(Parse("@macro2(); @alias(123)", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( ) ; replace ( 123 )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("none;@macro2(...)", macro)) << macro->Dump();
    ASSERT_STREQ("none ; replace2 ( ... )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@macro2(1,2,3);\n", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( 1 , 2 , 3 ) ;", LexOut().c_str());
    //    ASSERT_NO_THROW(Parse("@macro2(1,2,3);\nnone", macro)) << macro->Dump();
    //    ASSERT_STREQ("replace2 ( 1 , 2 , 3 ) ; none", LexOut().c_str());
}

TEST_F(MacroTest, Concat) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ concat $a $b @@ @$a @## @$b @@@@;", macro));
    ASSERT_NO_THROW(Parse("@concat hello world", macro));
    ASSERT_STREQ("helloworld", LexOut().c_str());
}

TEST_F(MacroTest, ToStr) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ show $x @@ @#' @$x @@@@;", macro));
    ASSERT_NO_THROW(Parse("@show 42", macro));
    ASSERT_STREQ("42", LexOut().c_str());
    ASSERT_TRUE(ast);
}

TEST_F(MacroTest, Recursion) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ A @@ B @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ B @@ 42 @@@@;", macro));

    ASSERT_NO_THROW(Parse("@A", macro));
    ASSERT_STREQ("42", LexOut().c_str());
}

TEST_F(MacroTest, MultiwordWithAt) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ MY NAME @@ result @@@@;", macro));
    ASSERT_NO_THROW(Parse("@MY NAME", macro));
    ASSERT_STREQ("result", LexOut().c_str());
}

TEST_F(MacroTest, MnemonicDefinition) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    // Мнемонические маркеры - обычные макросы с телом-лексемой (@\@@/@\@@@/@\@4).
    // Привязываются к токену и распознаются в Parser::GetNextToken по имени (Macro::MarkerToken),
    // а не правилом flex. Определяем их сырыми маркерами, как в DSL.
    ASSERT_NO_THROW(Parse("@@ @macro @@ @\\@@ @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ @macro_body @@ @\\@@ @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ @macro_text @@ @\\@@@ @@@@;", macro));
    ASSERT_NO_THROW(Parse("@@ @macro_end @@ @\\@4 @@@@;", macro));

    // seq-макрос через маркеры (открытие, имя-закрытие, конец тела):
    // @macro add2 $a $b @macro_body ( @$a + @$b ) @macro_end;
    ASSERT_NO_THROW(Parse("@macro add2 $a $b @macro_body ( @$a + @$b ) @macro_end;", macro));
    ASSERT_NO_THROW(Parse("@add2 3 4", macro));
    ASSERT_STREQ("( 3 + 4 )", LexOut().c_str());

    // текстовый макрос: открытие текста - маркер @macro_text, закрытие - сырой @@@
    // (текстовое тело читается в state_MACRO_STR, где маркеры по имени не распознаются).
    ASSERT_NO_THROW(Parse("@macro num $x @macro_text ( @$x ) @@@@;", macro));
    ASSERT_NO_THROW(Parse("@num 42", macro));
    ASSERT_STREQ("( 42 )", LexOut().c_str());
}

TEST_F(MacroTest, PredefVersionMacros) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@__TRUST_VERSION_MAJOR__", macro));
    ASSERT_STREQ(std::to_string(TRUST_VERSION_MAJOR).c_str(), LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__TRUST_VERSION_MINOR__", macro));
    ASSERT_STREQ(std::to_string(TRUST_VERSION_MINOR).c_str(), LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__TRUST_VERSION_PATCH__", macro));
    ASSERT_STREQ(std::to_string(TRUST_VERSION_PATCH).c_str(), LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__TRUST_VERSION__", macro));
    ASSERT_STREQ(TRUST_VERSION, LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__TRUST_GIT_HASH__", macro));
    ASSERT_STREQ(TRUST_GIT_HASH, LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__TRUST_VERSION_FULL__", macro));
    ASSERT_STREQ(TRUST_VERSION_FULL, LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__TRUST_DATE_BUILD__", macro));
    ASSERT_STREQ(TRUST_DATE_BUILD, LexOut().c_str());
}

TEST_F(MacroTest, PredefCounter) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@__COUNTER__", macro));
    ASSERT_STREQ("0", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__COUNTER__", macro));
    ASSERT_STREQ("1", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__COUNTER__", macro));
    ASSERT_STREQ("2", LexOut().c_str());
}

TEST_F(MacroTest, PredefFileLine) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("x @__LINE__;", macro));
    ASSERT_FALSE(LexOut().empty());

    ASSERT_NO_THROW(Parse("x @__FILE_LINE__;", macro));
    ASSERT_FALSE(LexOut().empty());

    ASSERT_NO_THROW(Parse("@__FILE__", macro));
    ASSERT_EQ(TermID::STRCHAR, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_STREQ("@input", LexOut().c_str());

    ASSERT_NO_THROW(Parse("@__FILE_NAME__", macro));
    ASSERT_STREQ("@input", LexOut().c_str());
}

TEST_F(MacroTest, PredefDate) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@__DATE__", macro));
    ASSERT_FALSE(LexOut().empty());

    ASSERT_NO_THROW(Parse("@__TIME__", macro));
    ASSERT_FALSE(LexOut().empty());

    ASSERT_NO_THROW(Parse("@__TIMESTAMP__", macro));
    ASSERT_FALSE(LexOut().empty());

    ASSERT_NO_THROW(Parse("@__TIMESTAMP_ISO__", macro));
    ASSERT_FALSE(LexOut().empty());
}

TEST_F(MacroTest, Hygienic) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ __H $x @@ @$x @__HYGIENIC__(tmp) @@@@;", macro));
    ASSERT_NO_THROW(Parse("@__H 42", macro));
    ASSERT_TRUE(LexOut().find("42") != std::string::npos);
}

TEST_F(MacroTest, HygienicQualified) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ __HQ $x @@ @$x @__HYGIENIC__(MyType::tmp) @@@@;", macro));
    ASSERT_NO_THROW(Parse("@__HQ 42", macro));
    ASSERT_TRUE(LexOut().find("42") != std::string::npos);
}
