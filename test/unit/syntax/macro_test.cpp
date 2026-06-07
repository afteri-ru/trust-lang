#include "syntax/warning_push.h"
#include <gtest/gtest.h>
#include "syntax/warning_pop.h"

#include "syntax/macro.h"
#include "syntax/lexer.h"
#include "syntax/parser.h"

using namespace trust;

class MacroTest : public ::testing::Test {
  protected:
    trust::Context m_ctx;
    std::vector<std::string> m_postlex;

    std::string m_output;

    void SetUp() { m_ctx.diag().clear(); }

    void TearDown() {}

    TermPtr Parse(std::string str, MacroPtr buffer = nullptr) {
        m_postlex.clear();
        if (buffer)
            m_ctx.setMacro(buffer);
        Parser p(m_ctx, &m_postlex);
        ast = p.ParseText(str);
        return ast;
    }

    int Count(TermID token_id) {
        int result = 0;
        for (int c = 0; c < ast->size(); c++) {
            if (ast->at(c).second->m_id == token_id) {
                result++;
            }
        }
        return result;
    }

    std::string LexOut() {
        std::string result;
        for (int i = 0; i < m_postlex.size(); i++) {
            if (!result.empty()) {
                result += " ";
            }
            result += m_postlex[i];
        }
        trim(result);
        return result;
    }

    TermPtr ast;
};

/*
 * Макросы хрянтся как фрагменты AST в виде объектов типа Term,
 * так как создаются после обработки парсером по правилам стандратной грамматики.
 *
 * Но анализ входных данных на предмет раскрытия макросов проиходит из потока (последовательности) лексем,
 * т.е. **ДО** обработки парсером, так как при раскрытии макросов может меняться сама грамматика выражений.
 *
 * Функция \ref IdentityMacro сравнивает входной буфер (последовательность лексем)
 * на предмет возможного соответствия одному конкретному макросу.
 * Сравниваются только ключевые словам без учета аргументов в скобках, только наличие или отсуствие скобок,
 * так с аргументами и без аргументов, это два разных макроса.
 * Проверка аргументов макроса выполняется в функции \ref ExtractArgs
 *
 * Проблема:
 * К функции можно обратится с целью её вызова (указав после имени скобки),
 * так и по имени без скобок (например для получения ссылки на объект).
 * Так и к переменной можно обратиться только по имени,
 * так и указав скобки после имени как к функции (при копировании объекта).
 *
 * Сделать один макрос под оба сценария (со скобками и без скобок) не получится, так как непонятно,
 * что делать с аргументами при раскрытии тела макроса, когда они не указаны.
 *
 * Но может быть следует переименовывать только одно имя без учета скобок,
 * например, оставив такую возможность только для одиночных макросов.
 *
 *
 * @macro() := with_bracket;
 * @macro := without_bracket;
 *   или
 * @macro :- ignore_bracket;
 *
 * macro() -> ignore_bracket();
 * macro -> ignore_bracket;
 *
 *
 * \\name -> name или name (...), hash: name
 * но \\name(...) -> name(...), а name - ошибка !!!!  hash: name
 *
 * Или все же следует различать макросы со скобками и без скобок как два разных объекта???
 * \\name := name2;  и \\name(...) := name2(...); будут разными объектами
 *
 * Или добавить макросы-алиасы без аргументов только для переименования отдельных терминов?
 * @alias :- name2; но как их отличать от обычных макросов в операции удаления?
 * Или вообще не нужно отличать и оставить только один варинат (либо макрос, либо алиас)
 * Тода вопрос со скобками закрывается сам собой, алиасы всегда без скобок,
 * макросы со скбками или без скобок, но должно быть полное соответствие.
 *
 * @@ name name2 @@ -> name name2, но name name2(...) - ошибка!!! (hash: name,name2)
 * @@ name name2(...) @@ -> name name2(...), но name name2 - ошибка !!!! (hash: name,name2)
 * @@ name name2[...](...) @@ -> name name2[...](...), но name name2 - ошибка !!!!  ( hash: name,name2 )
 * @@ name $tmpl[...](...) name3@@ -> name $tmpl[...](...) name3, но name $tmpl name3 - ошибка !!!!  ( hash: name,$,name3 )
 *
 * Но!
 * @@ name name2 @@ ::-  -> name name2 - ок, но name name2(...) - ок
 * @@ name $tmpl name3 @@ ::- -> name $tmpl[...](...) name3 - ок, name $tmpl(...) name3[...] - ок
 *
 *
 *
 * Для последовательности лексем требуется полное соответствие с учетом скобок ???
 * и может ли быть несколько скобок одного типа (несколько круглых, или несколько квадратных????
 * <Может быть несколько скобок одного типа, например, при указании типа у аргументов или типа возвращаемого значения.>
 *
 * @@ name name2 @@ -> name name2, но name name2(...) - ошибка!!! (hash: name,name2)
 * @@ name name2(...) @@ -> name name2(...), но name name2 - ошибка !!!! (hash: name,name2)
 * @@ name name2[...](...) @@ -> name name2[...](...), но name name2 - ошибка !!!!  ( hash: name,name2 )
 * @@ name $tmpl[...](...) name3 @@ -> name $tmpl[...](...) name3, но name $tmpl name3 - ошибка !!!!  ( hash: name,$,name3 )
 *
 * Проблема скобок возникает из-за сценария замены одного термина на другой,
 * который есть в препроцессоре С/С++, но отсутствует при реализации с использованием шаблонов.
 *
 *
 *
 *
 * ```python

try:
    a = float(input("Введите число: ")
    print (100 / a)
except ValueError:
    print ("Это не число!")
except ZeroDivisionError:
    print ("На ноль делить нельзя!")
except:
    print ("Неожиданная ошибка.")
else:
    print ("Код выполнился без ошибок")
finally:
    print ("Я выполняюсь в любом случае!")

```
```

[1] =>{

    [1] --> {

    },[2] --> {*

 *},[...] --> {
        other
    }
    on_exit();
};


{*   # try:
    a = float(input("Введите число: ");
    print (100 / a);
 *} ~> {

    [:ValueError] --> print ("Это не число!"),  # except ValueError:
    [:ZeroDivisionError] --> print ("На ноль делить нельзя!"), # except ZeroDivisionError
    [:IntMinus] --> print ("Неожиданная ошибка."), # except:
    [...] --> print ("Код выполнился без ошибок");  # else:

    print ("Я выполняюсь в любом случае!");    # finally:
 * try := @ __TERM_EXPECTED__('{*') @
 *
 * catch := @ *} ~> @
 *
 *
 */

/*
 @##
 @#

@func(...) := call(?);

@$name

@$name(...)
@$name[...]
@$name<...>

@$name(*)
@$name[*]
@$name<*>

@$name(#)
@$name[#]
@$name<#>

 *  */

TEST_F(MacroTest, ParseTerm) {

    TermPtr term;
    BlockType buff;
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
    buff.push_back(Term::Create(TermID::SYMBOL, "(", {}, parser::token_type::SYMBOL)); // second (

    ASSERT_ANY_THROW(Parser::ParseTerm(term, buff, m_ctx, 0));

    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL)); // second ( )

    ASSERT_NO_THROW(size = Parser::ParseTerm(term, buff, m_ctx, 0));
    ASSERT_EQ(3, size);
    ASSERT_TRUE(term);
    ASSERT_TRUE(term->isCall());
    ASSERT_EQ("second()", term->toString());

    buff.erase(buff.end()); // second (

    buff.push_back(Term::Create(TermID::NAME, "name", {}, parser::token_type::NAME)); // second ( name
    ASSERT_ANY_THROW(Parser::ParseTerm(term, buff, m_ctx, 0));

    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL)); // second ( name )

    ASSERT_NO_THROW(size = Parser::ParseTerm(term, buff, m_ctx, 0));
    ASSERT_EQ(4, size);
    ASSERT_TRUE(term);
    ASSERT_TRUE(term->isCall());
    ASSERT_EQ("second(name)", term->toString());

    buff.erase(buff.end());                                                            // second ( name
    buff.push_back(Term::Create(TermID::SYMBOL, "=", {}, parser::token_type::SYMBOL)); // second ( name =
    ASSERT_ANY_THROW(Parser::ParseTerm(term, buff, m_ctx, 0));

    buff.push_back(Term::Create(TermID::NAME, "value", {}, parser::token_type::NAME)); // second ( name = value
    ASSERT_ANY_THROW(Parser::ParseTerm(term, buff, m_ctx, 0));

    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL)); // second ( name = value )

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

TEST_F(MacroTest, DISABLED_Annotate) {

    // Logger callback removed — errors are printed to stderr, not captured in m_output.
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
    BlockType buffer;
    MacroPtr macro_buf = std::make_shared<Macro>(m_ctx);

    ASSERT_STREQ("name", macro_buf->toMacroHashName("name").c_str());
    ASSERT_STREQ("$", macro_buf->toMacroHashName("$name").c_str());
    ASSERT_STREQ("name", macro_buf->toMacroHashName("@name").c_str());

    ASSERT_FALSE(macro_buf->IdentityMacro(buffer, term));

#define CREATE_TERM(type, text) Term::Create(TermID::type, text, {}, parser::token_type::type)

    term = Parse("@@ macro @@ := @@ name @@", macro_buf);
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
    term->m_left->m_block[0]->getText() = "macro2";
    ASSERT_FALSE(macro_buf->IdentityMacro(buffer, term));

    ASSERT_EQ(2, buffer.size());
    buffer.erase(buffer.begin(), buffer.begin() + 1);
    ASSERT_EQ(1, buffer.size());
    ASSERT_TRUE(macro_buf->IdentityMacro(buffer, term));

    TermPtr hash = Parse("@@ name1 name2 @@ := @@ @@", macro_buf);
    ASSERT_TRUE(hash);
    ASSERT_TRUE(hash->isMacro());
    ASSERT_TRUE(hash->m_left);
    ASSERT_TRUE(hash->m_right);

    ASSERT_STREQ("name1", macro_buf->toMacroHash(hash).c_str());

    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_TRUE(macro->isEmpty());

    ASSERT_TRUE(term = Parse("@@alias@@ := alias_name", macro));
    ASSERT_STREQ("@@ alias @@ := alias_name", LexOut().c_str());
    ASSERT_TRUE(term);
    ASSERT_EQ("@@ alias @@ := alias_name;", term->toString());
    ASSERT_TRUE(term->m_left->m_block[0]);
    ASSERT_EQ(1, term->m_left->m_block.size());
    ASSERT_TRUE(term->m_left->m_block[0]);
    ASSERT_EQ("alias", term->m_left->m_block[0]->toString());

    BlockType id = macro->GetMacroId(macro->FindMacroList("alias")->at(0));
    ASSERT_EQ(1, id.size()) << macro->FindMacroList("alias")->at(0)->toString().c_str();
    ASSERT_EQ("alias", id[0]->getText());

    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias"})) << macro->Dump();

    // FAIL REDEFINE
    ASSERT_ANY_THROW(Parse("@@alias@@ ::= alias_name2;", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    //    ASSERT_TRUE(macro->GetMacro({"alias"})) << macro->Dump();
    //    ASSERT_STREQ("@@ alias @@ ::= alias_name2 ;", LexOut().c_str());

    //    ASSERT_ANY_THROW(Parse("@alias+alias := alias_name", macro)) << macro->Dump();
    //    ASSERT_EQ(1, macro->GetCount());

    ASSERT_TRUE(term = Parse("@@alias2@@ := alias_name", macro));
    ASSERT_EQ(2, macro->CountInScope(0));
    ASSERT_STREQ("@@ alias2 @@ := alias_name", LexOut().c_str());

    ASSERT_TRUE(term->m_left);
    ASSERT_EQ(1, term->m_left->m_block.size());
    ASSERT_TRUE(term->m_left->m_block[0]);
    ASSERT_EQ("alias2", term->m_left->m_block[0]->toString());

    ASSERT_ANY_THROW(Parse("@@@@ @@@@", macro));

    ASSERT_TRUE(Parse("@@@@alias@@@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_STREQ("@@@@ alias @@@@", LexOut().c_str());

    ASSERT_TRUE(Parse("@@@@_@@@@;", macro));
    ASSERT_EQ(0, macro->CountInScope(0)) << macro->Dump();
    ASSERT_STREQ("@@@@ _ @@@@ ;", LexOut().c_str());

    ASSERT_TRUE(term = Parse("@@if(args)@@ := @@ [@$args] --> @@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_STREQ("@@ if ( args ) @@ := @@ [ @$args ] --> @@", LexOut().c_str());

    ASSERT_TRUE(term->m_left);
    ASSERT_EQ(4, term->m_left->m_block.size());
    ASSERT_EQ(1, macro->GetMacroId(term).size());
    ASSERT_EQ("if(args)", macro->GetMacroId(term)[0]->toString());

    id = macro->GetMacroId(macro->FindMacroList("if")->at(0));
    ASSERT_EQ(1, id.size()) << macro->FindMacroList("if")->at(0)->toString().c_str();
    ASSERT_EQ("if", id[0]->getText());

    ASSERT_TRUE(macro->GetMacro({"if"})) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"if"})->m_right);
    ASSERT_EQ(4, macro->GetMacro({"if"})->m_right->m_block.size()) << macro->GetMacro({"if"})->m_right->toString().c_str();

    ASSERT_TRUE(term = Parse("@@if2(...)@@ := @@@ [ @__LINE__ ] --> @@@", macro));
    ASSERT_STREQ("@@ if2 ( ... ) @@ :=  [ @__LINE__ ] -->", LexOut().c_str());

    ASSERT_TRUE(term = Parse("@@if2(...)@@ := @@ [ @__LINE__ ] --> @@", macro));
    ASSERT_STREQ("@@ if2 ( ... ) @@ := @@ [ @__LINE__ ] --> @@", LexOut().c_str());

    ASSERT_TRUE(term->m_left);
    ASSERT_EQ(4, term->m_left->m_block.size());
    BlockType id1 = macro->GetMacroId(term);
    ASSERT_EQ(1, id1.size());
    ASSERT_TRUE(id1[0]);
    ASSERT_EQ("if2(...)", id1[0]->toString());

    ASSERT_EQ(2, macro->CountInScope(0));
    ASSERT_TRUE(macro->GetMacro({"if2"}));
    ASSERT_TRUE(macro->GetMacro({"if2"})->m_right);
    ASSERT_EQ("@@ [ @__LINE__ ] --> @@", macro->GetMacro({"if2"})->m_right->toString());

    ASSERT_TRUE(term = Parse("@@ func $name(arg= @__LINE__ , ...) @@ := @@@ [ @__LINE__ ] --> @@@", macro));
    ASSERT_STREQ("@@ func $name ( arg = @__LINE__ , ... ) @@ :=  [ @__LINE__ ] -->", LexOut().c_str());

    BlockType id2 = macro->GetMacroId(term);
    ASSERT_EQ(2, id2.size());
    ASSERT_TRUE(id2[0]);
    ASSERT_EQ("func", id2[0]->toString());
    ASSERT_TRUE(id2[1]);
    ASSERT_EQ("$name(arg=1, ...)", id2[1]->toString());

    ASSERT_EQ(3, macro->CountInScope(0));
    ASSERT_TRUE(macro->GetMacro(std::vector<std::string>({"func", "$"})));
    ASSERT_TRUE(macro->GetMacro(std::vector<std::string>({"func", "$"}))->m_right);
    ASSERT_EQ("@@@ [ @__LINE__ ] --> @@@", macro->GetMacro(std::vector<std::string>({"func", "$"}))->m_right->toString());

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
    ASSERT_NO_THROW(Parse("@@a@@ := 1;", macro));
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

    ASSERT_TRUE(Parse("@@alias replace@@ := @@replace@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(Parse("@@alias second@@ := @@second@@", macro));
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(Parse("@@text@@ := @@@text;\n text@@@", macro));
    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(Parse("@@dsl@@ := @@@ @@m1@@ := @@mm@@;\n  @@m2@@ := @@mm@@;\n@@@", macro));

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

    BlockType buff;
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

    buff.push_back(Term::Create(TermID::SYMBOL, "(", {}, parser::token_type::SYMBOL)); // alias alias second (

    ASSERT_FALSE(macro->IdentityMacro(buff, macro_replace)); // alias replace
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second));  // alias second
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));    // text
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));     // dsl

    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL)); // alias alias second ( )

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

    ASSERT_NO_THROW(Parse("@@alias@@ ::= @@replace@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@alias()@@ := @@replace@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_ANY_THROW(Parse("@@alias(...)@@ ::= @@error@@", macro));
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@second(...)@@ := @@second2(@$#, @$...)@@", macro));
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();

    // second(...) and second are distinct macros (different number of id elements)
    ASSERT_NO_THROW(Parse("@@second@@ := @@second2(@$#, @$*)@@", macro));
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@text(...)@@ := @@text1(@$#, @$*)@@", macro));
    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();
    ASSERT_NO_THROW(Parse("@@dsl@@ := @@@\n @@m1@@ := @@mm@@;\n @@m2@@ := @@mm@@;\n@@@", macro));

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

    BlockType buff;                                         //
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

    buff.push_back(Term::Create(TermID::SYMBOL, "(", {}, parser::token_type::SYMBOL)); // alias alias second (

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));   // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl

    buff.push_back(Term::Create(TermID::NAME, "arg", {}, parser::token_type::NAME)); // alias alias second ( arg

    ASSERT_TRUE(macro->IdentityMacro(buff, macro_alias));   // alias
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_second)); // second(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_text));   // text(...)
    ASSERT_FALSE(macro->IdentityMacro(buff, macro_dsl));    // dsl()

    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL)); // alias alias second ( arg )

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
    ASSERT_EQ("replace", ast->toString());

    ASSERT_NO_THROW(Parse("alias", macro));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("replace", ast->getText());

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

    ASSERT_ANY_THROW(Parse("second", macro));
    ASSERT_ANY_THROW(Parse("@second", macro));

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
    //     ASSERT_NO_THROW(Parse("@@alias@@ := @@replace@@", macro));
    //     ASSERT_EQ(1, macro->GetCount()) << macro->Dump();
    //     ASSERT_NO_THROW(Parse("@@alias second(...)@@ := @@second(@$#, @$*)@@", macro));
    //     ASSERT_EQ(2, macro->GetCount()) << macro->Dump();
    //     ASSERT_NO_THROW(Parse("@@text(...)@@ := @@@text1(@$#, @$*);\n text1@@@", macro));
    //     ASSERT_EQ(3, macro->GetCount()) << macro->Dump();
    //     ASSERT_NO_THROW(Parse("@@dsl()@@ := @@@ @@m1@@ := @@mm@@;\n  @@m2@@ := @@mm@@;\n@@@", macro));
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
    //     BlockType buff; //
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
    //     buff.push_back(Term::Create(parser::token_type::SYMBOL, TermID::SYMBOL, "(")); // alias alias second (
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
    //     buff.push_back(Term::Create(parser::token_type::SYMBOL, TermID::SYMBOL, ")")); // alias alias second ( arg )
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

    ASSERT_ANY_THROW(Parse("@@@@ macro @@  @@@@"));
    ASSERT_ANY_THROW(Parse("@@@@ @@  macro  @@@@"));
    ASSERT_ANY_THROW(Parse("@@  macro @@@@  @@"));
    ASSERT_ANY_THROW(Parse("@@  @@@@  macro  @@"));
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@  @macro  @@"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("@@  @$macro  @@"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);

    ASSERT_TRUE(Parse("@@@@ alias  @@@@", macro));
    ASSERT_EQ(0, macro->CountInScope(0));
    ASSERT_TRUE(Parse("@@@@ alias $alias2 @@@@", macro));
    ASSERT_EQ(0, macro->CountInScope(0));

    ASSERT_TRUE(Parse("@@alias@@ ::= @@replace@@", macro));
    ASSERT_TRUE(Parse("@@alias2@@ := @@alias@@", macro));
    ASSERT_TRUE(Parse("@@fail@@ := @@fail@@", macro));

    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias"}));
    TermPtr macro_alias = macro->GetMacro({"alias"});
    ASSERT_TRUE(macro_alias);
    ASSERT_EQ(TermID::CREATE_TYPE, macro_alias->getTermID()) << toString(macro_alias->getTermID());
    ASSERT_TRUE(macro_alias->m_left);
    ASSERT_EQ(TermID::MACRO_SEQ, macro_alias->m_left->getTermID()) << toString(macro_alias->m_left->getTermID());
    ASSERT_TRUE(macro_alias->m_right);
    ASSERT_TRUE(macro_alias->m_right->m_block.size()) << macro_alias->m_right->toString();
    ASSERT_EQ("replace", macro_alias->m_right->m_block[0]->getText());

    ASSERT_TRUE(macro->GetMacro({"alias2"})) << macro->Dump();
    TermPtr macro_alias2 = macro->GetMacro({"alias2"});
    ASSERT_TRUE(macro_alias2);
    ASSERT_TRUE(macro_alias2->isMacro());
    ASSERT_EQ(TermID::CREATE_NAME, macro_alias2->getTermID()) << toString(macro_alias2->getTermID());
    ASSERT_TRUE(macro_alias2->m_left);
    ASSERT_EQ(TermID::MACRO_SEQ, macro_alias2->m_left->getTermID()) << toString(macro_alias2->m_left->getTermID());
    ASSERT_EQ("alias", macro_alias2->m_right->m_block[0]->getText());

    ASSERT_TRUE(macro->GetMacro({"fail"})) << macro->Dump();
    TermPtr macro_fail = macro->GetMacro({"fail"});
    ASSERT_TRUE(macro_fail);
    ASSERT_TRUE(macro_fail->m_left);
    ASSERT_EQ(TermID::MACRO_SEQ, macro_fail->m_left->getTermID());
    ASSERT_EQ("fail", macro_fail->m_right->m_block[0]->getText());

    TermPtr term = Term::Create(TermID::NAME, "alias", {}, parser::token_type::NAME);

    ASSERT_TRUE(macro->FindMacroList(term->getText()));

    BlockType vals = *macro->FindMacroList(term->getText());
    ASSERT_EQ(1, vals.size());

    BlockType buff;
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

    Macro::MacroArgsType macro_args;

    ASSERT_EQ(1, macro->ExtractArgs(buff, macro_alias, macro_args));
    ASSERT_EQ(3, macro_args.size()) << macro->Dump(macro_args);

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_fail, macro_args));

    // macro_alias has m_right of type MACRO_SEQ, ExpandString requires MACRO_STR
    // Use ExpandMacros instead
    BlockType block;
    block = macro->ExpandMacros(macro_alias, macro_args);
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
    BlockType buffer;

    BlockType vect;
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
    //    ASSERT_EQ(1, macro_alias1->m_right->m_block.size());
    //    ASSERT_STREQ("replace1", macro_alias1->m_right->m_block[0]->getText());

    ASSERT_EQ(0, macro->CountInScope(0));

    ASSERT_NO_THROW(Parse("@@alias@@ := @@ replace1 @@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_ANY_THROW(Parse("@@alias@@ ::= @@replace2@@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_NO_THROW(Parse("@@alias@@ = @@replace3@@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_ANY_THROW(Parse("@@alias(arg)@@ ::= @@ replace2(@$arg) @@", macro)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@@@ alias @@@@", macro)) << macro->Dump();
    ASSERT_EQ(0, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@alias(arg, ... )@@ := @@ replace2(@$arg) @@", macro)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias"}));
    ASSERT_EQ("@@ alias ( arg , ... ) @@ := @@ replace2 ( @$arg ) @@;", macro->GetMacro({"alias"})->toString());

    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_ANY_THROW(Parse("@@alias(arg, ... )@@ ::= @@ replace3(@$arg) @@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@alias(arg, ... )@@ = @@ replace4(@$arg) @@", macro)) << macro->Dump();
    ASSERT_EQ(1, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"alias"}));
    ASSERT_EQ("@@ alias ( arg , ... ) @@ := @@ replace4 ( @$arg ) @@;", macro->GetMacro({"alias"})->toString());

    ASSERT_NO_THROW(Parse("@@alias3(...)@@ := @@replace3(@$#, @$...)@@", macro)) << macro->Dump();
    ASSERT_EQ(2, macro->CountInScope(0)) << macro->Dump();

    ASSERT_ANY_THROW(Parse("@@alias3(...)@@ ::= @@ error double macros@@", macro)) << macro->Dump();
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

    ASSERT_NO_THROW(Parse("@@macro(arg, ... )@@ ::= @@@ 3*@$arg @@@", macro)) << macro->Dump();
    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();

    BlockType* alias_list = macro->FindMacroList("alias");
    ASSERT_TRUE(alias_list);

    vect = *alias_list;

    ASSERT_EQ(1, vect.size());

    macro_alias1 = vect[0];
    ASSERT_TRUE(macro_alias1);
    ASSERT_EQ("@@ alias ( arg , ... ) @@ := @@ replace4 ( @$arg ) @@;", macro_alias1->toString());
    ASSERT_TRUE(macro_alias1->m_right);
    ASSERT_EQ(4, macro_alias1->m_right->m_block.size());
    ASSERT_EQ("replace4", macro_alias1->m_right->m_block[0]->getText());

    //    TermPtr macro_alias2 = vect[1];
    //    ASSERT_TRUE(macro_alias2);
    //    ASSERT_EQ("alias", macro_alias2->getText());
    //    ASSERT_TRUE(macro_alias2->getTermID() == TermID::MACRO_SEQ) << macro_alias2->toString().c_str();
    //    ASSERT_EQ(4, macro_alias2->m_block.size());
    //    ASSERT_TRUE(macro_alias2->m_right);
    //    ASSERT_EQ(4, macro_alias2->m_right->m_block.size()) << macro_alias2->m_right->m_block[0]->getText();
    //    ASSERT_STREQ("replace2", macro_alias2->m_right->m_block[0]->getText());
    //    ASSERT_STREQ("(", macro_alias2->m_right->m_block[1]->getText());
    //    ASSERT_STREQ("@$arg", macro_alias2->m_right->m_block[2]->getText());
    //    ASSERT_STREQ(")", macro_alias2->m_right->m_block[3]->getText());

    //    TermPtr macro_alias3 = vect[2];
    //    ASSERT_TRUE(macro_alias3);
    //    ASSERT_EQ("alias", macro_alias3->getText());
    //    ASSERT_TRUE(macro_alias3->getTermID() == TermID::MACRO_SEQ) << macro_alias3->toString().c_str();
    //    ASSERT_EQ(5, macro_alias3->m_block.size());
    //    ASSERT_STREQ("(", macro_alias3->m_block[1]->getText());
    //    ASSERT_TRUE(macro_alias3->m_right);
    //    ASSERT_EQ(4, macro_alias3->m_right->m_block.size());
    //    ASSERT_EQ("replace3", macro_alias3->m_right->m_block[0]->getText());
    //    ASSERT_STREQ("(", macro_alias3->m_right->m_block[1]->getText());
    //    ASSERT_STREQ("@$*", macro_alias3->m_right->m_block[2]->getText());
    //    ASSERT_STREQ(")", macro_alias3->m_right->m_block[3]->getText());

    //    ASSERT_EQ(macro_alias1.get(), macro->GetMacro({"alias"}).get()) << macro->Dump();
    //    //    ASSERT_EQ(macro_alias2.get(), macro->GetMacro({"alias", "second"}).get()) << macro->Dump();
    //    //    ASSERT_EQ(macro_alias3.get(), macro->GetMacro({"alias", "(", "$", ")", "second"}).get()) << macro->Dump();
    //
    //
    BlockType* macro_list = macro->FindMacroList("macro");
    ASSERT_TRUE(macro_list);

    vect = *macro_list;
    ASSERT_EQ(1, vect.size());
    TermPtr macro_macro1 = vect[0];
    ASSERT_TRUE(macro_macro1);
    ASSERT_EQ("@@ macro ( arg , ... ) @@ ::= @@@ 3*@$arg @@@;", macro_macro1->toString());
    ASSERT_EQ(macro_macro1.get(), macro->GetMacro({"macro"}).get()); // Поиск по MacroID и возврат TermPtr
    ASSERT_TRUE(macro_macro1->m_right);
    ASSERT_TRUE(macro_macro1->m_right->getTermID() == TermID::MACRO_STR) << macro_macro1->toString().c_str();
    //

    BlockType buff;
    Macro::MacroArgsType macro_args;

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_alias1, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::NAME, "alias", {}, parser::token_type::NAME));

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_alias1, macro_args)) << macro_alias1->toString().c_str();

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args)) << macro_macro1->toString().c_str();

    buff.push_back(Term::Create(TermID::SYMBOL, "(", {}, parser::token_type::SYMBOL));

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_alias1, macro_args));

    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL));

    size_t count;
    ASSERT_NO_THROW(count = macro->ExtractArgs(buff, macro_alias1, macro_args));
    ASSERT_EQ(3, count);

    buff.erase(buff.end());

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::NAME, "value", {}, parser::token_type::NAME));

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_alias1, macro_args));

    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL));

    ASSERT_EQ(4, buff.size());
    ASSERT_NO_THROW(count = macro->ExtractArgs(buff, macro_alias1, macro_args)) << macro->Dump(buff);
    ASSERT_EQ(4, count);
    buff.erase(buff.end());

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::SYMBOL, ",", {}, parser::token_type::SYMBOL));

    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //    ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));
    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::NAME, "value2", {}, parser::token_type::NAME));

    buff.push_back(Term::Create(TermID::NAME, "value3", {}, parser::token_type::NAME));

    ASSERT_ANY_THROW(macro->ExtractArgs(buff, macro_macro1, macro_args));

    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL));

    ASSERT_NO_THROW(ASSERT_EQ(7, macro->ExtractArgs(buff, macro_alias1, macro_args)););
    ASSERT_EQ(6, macro_args.size()) << macro->Dump(macro_args);

    //        ASSERT_EQ(7, MacroBuffer::ExtractArgs(buff, macro_alias2, macro_args));
    //        ASSERT_EQ(4, macro_args.size()) << MacroBuffer::Dump(macro_args);

    ASSERT_EQ(7, buff.size());
    ASSERT_NO_THROW(ASSERT_EQ(7, macro->ExtractArgs(buff, macro_macro1, macro_args)););
    ASSERT_EQ(6, macro_args.size()) << macro->Dump(macro_args);

    //        ASSERT_ANY_THROW(MacroBuffer::ExtractArgs(buff, macro_alias3, macro_args));

    buff.push_back(Term::Create(TermID::SYMBOL, ";", {}, parser::token_type::SYMBOL));

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
    //    BlockType res = MacroBuffer::ExpandMacros(macro_alias1, macro_args);
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
    buff.push_back(Term::Create(TermID::SYMBOL, "(", {}, parser::token_type::SYMBOL));
    buff.push_back(Term::Create(TermID::NUMBER, "5", {}, parser::token_type::NUMBER));
    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL));
    buff.push_back(Term::Create(TermID::SYMBOL, ";", {}, parser::token_type::SYMBOL));

    TermPtr macro_macro = macro->GetMacro({"macro"});
    ASSERT_TRUE(macro_macro);

    ASSERT_NO_THROW(ASSERT_EQ(4, macro->ExtractArgs(buff, macro_macro, macro_args)) << macro->Dump(macro_args););
    ASSERT_EQ(5, macro_args.size()) << macro->Dump(macro_args);

    std::string str = macro->ExpandString(macro_macro, macro_args);
    ASSERT_STREQ(" 3*5  ", str.c_str());

    buff.clear();
    buff.push_back(Term::Create(TermID::NAME, "alias3", {}, parser::token_type::NAME));
    buff.push_back(Term::Create(TermID::SYMBOL, "(", {}, parser::token_type::SYMBOL));
    buff.push_back(Term::Create(TermID::NUMBER, "5", {}, parser::token_type::NUMBER));
    buff.push_back(Term::Create(TermID::SYMBOL, ")", {}, parser::token_type::SYMBOL));
    buff.push_back(Term::Create(TermID::SYMBOL, ";", {}, parser::token_type::SYMBOL));

    TermPtr macro_alias3 = macro->GetMacro({"alias3"});
    ASSERT_TRUE(macro_alias3);

    ASSERT_EQ("@@ alias3 ( ... ) @@ := @@ replace3 ( @$# , @$... ) @@;", macro_alias3->toString());
    ASSERT_TRUE(macro_alias3->m_right);
    ASSERT_EQ(6, macro_alias3->m_right->m_block.size());
    ASSERT_EQ("replace3", macro_alias3->m_right->m_block[0]->getText());

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
    BlockType blk = macro->ExpandMacros(macro_alias3, macro_args);
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

TEST_F(MacroTest, MacroCheck) {

    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    BlockType buffer;

    ASSERT_TRUE(macro->isEmpty());

    ASSERT_ANY_THROW(Parse("@@testargs(arg)@@ ::= @@ @$bad_arg @@", macro)) << macro->Dump();
    ASSERT_ANY_THROW(Parse("@@testargs(arg)@@ ::= @@ @$... @@", macro)) << macro->Dump();
    ASSERT_ANY_THROW(Parse("@@testargs(arg, ...)@@ ::= @@ @$2 @@", macro)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@ macro2(...) @@ ::= @@ replace2( @$#, @$... ,@$* ) @@", macro)) << macro->Dump();
    ASSERT_NO_THROW(Parse("macro2(1,9)", macro)) << macro->Dump() << LexOut().c_str();
    ASSERT_STREQ("replace2 ( 2 , 1 , 9 , ( 1 , 9 , ) )", LexOut().c_str());

    ASSERT_ANY_THROW(Parse("@@ return $... $... @@ ::= @@ @$... @@", macro)) << macro->Dump();
    ASSERT_ANY_THROW(Parse("@@ return() $... @@ ::= @@ @$... @@", macro)) << macro->Dump();

    ASSERT_NO_THROW(Parse("@@ return $... @@ ::= @@ :: ++ @$... ++ @@", macro)) << macro->Dump();
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
    //     macro = "@@return    --@@@";
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
    //             "@dowhile(cond)@@@"
    //             "@@@"
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
    BlockType buffer;

    ASSERT_TRUE(macro->isEmpty());

    ASSERT_NO_THROW(Parse("@@alias@@ := @@ replace @@", macro)) << macro->Dump();
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

    ASSERT_NO_THROW(Parse("@@ macro1 @@ ::= @@ replace1 @@", macro)) << macro->Dump();
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

    ASSERT_NO_THROW(Parse("@@ macro2(...) @@ ::= @@ replace2( @$... ) @@", macro)) << macro->Dump();
    ASSERT_EQ(3, macro->CountInScope(0)) << macro->Dump();
    ASSERT_TRUE(macro->GetMacro({"macro2"}));
    ASSERT_TRUE(macro->GetMacro({"macro2"})->m_left);
    ASSERT_EQ(TermID::MACRO_SEQ, macro->GetMacro({"macro2"})->m_left->getTermID());

    ASSERT_ANY_THROW(Parse("macro2", macro)) << macro->Dump();

    ASSERT_NO_THROW(Parse("macro2()", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("macro2()", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("macro2( 1 )", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( 1 )", LexOut().c_str());

    ASSERT_NO_THROW(Parse("macro2(1,2,3)", macro)) << macro->Dump();
    ASSERT_STREQ("replace2 ( 1 , 2 , 3 )", LexOut().c_str());

    ASSERT_ANY_THROW(Parse("@macro2", macro)) << macro->Dump();

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

    ASSERT_NO_THROW(Parse("@@ concat $a $b @@ := @@ @$a @## @$b @@;", macro));
    ASSERT_NO_THROW(Parse("@concat hello world", macro));
    ASSERT_STREQ("helloworld", LexOut().c_str());
}

TEST_F(MacroTest, ToStr) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ show $x @@ := @@ @#' @$x @@;", macro));
    ASSERT_NO_THROW(Parse("@show 42", macro));
    ASSERT_STREQ("42", LexOut().c_str());
    ASSERT_TRUE(ast);
}

TEST_F(MacroTest, Recursion) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ A @@ := @@ B @@;", macro));
    ASSERT_NO_THROW(Parse("@@ B @@ := 42;", macro));

    ASSERT_NO_THROW(Parse("@A", macro));
    ASSERT_STREQ("42", LexOut().c_str());
}

TEST_F(MacroTest, MultiwordWithAt) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ MY NAME @@ := result;", macro));
    ASSERT_NO_THROW(Parse("@MY NAME", macro));
    ASSERT_STREQ("result", LexOut().c_str());
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
    ASSERT_EQ(TermID::STRWIDE, ast->getTermID()) << trust::toString(ast->getTermID());
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

    ASSERT_NO_THROW(Parse("@@ __H $x @@ := @@ @$x @__HYGIENIC__(tmp) @@;", macro));
    ASSERT_NO_THROW(Parse("@__H 42", macro));
    ASSERT_TRUE(LexOut().find("42") != std::string::npos);
}

TEST_F(MacroTest, HygienicQualified) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ __HQ $x @@ := @@ @$x @__HYGIENIC__(MyType::tmp) @@;", macro));
    ASSERT_NO_THROW(Parse("@__HQ 42", macro));
    ASSERT_TRUE(LexOut().find("42") != std::string::npos);
}

// ── @__OPTION_PUSH__ / @__OPTION__ / @__OPTION_POP__ ──

TEST_F(MacroTest, OptionMacroRedefinedIgnore) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@__OPTION_PUSH__;", macro));
    ASSERT_NO_THROW(Parse("@__OPTION__(\"macro-redefined\", \"ignore\");", macro));

    // ::= — запрещённое по умолчанию переопределение, с ignore — молча игнорируется
    ASSERT_NO_THROW(Parse("@@ A @@ ::= 1;", macro));
    ASSERT_NO_THROW(Parse("@@ A @@ ::= 2;", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();

    ASSERT_NO_THROW(Parse("@__OPTION_POP__;", macro));
}

TEST_F(MacroTest, OptionMacroRedefinedErrorByDefault) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_NO_THROW(Parse("@@ B @@ ::= 1;", macro));
    m_ctx.diag().clear();
    // По умолчанию macro-redefined имеет severity Fatal — переопределение запрещено
    ASSERT_ANY_THROW(Parse("@@ B @@ ::= 2;", macro)) << macro->Dump();
}

TEST_F(MacroTest, OptionPushPopRestores) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    // ignore внутри push/pop
    ASSERT_NO_THROW(Parse("@__OPTION_PUSH__;", macro));
    ASSERT_NO_THROW(Parse("@__OPTION__(\"macro-redefined\", \"ignore\");", macro));
    ASSERT_NO_THROW(Parse("@@ C @@ ::= 1;", macro));
    ASSERT_NO_THROW(Parse("@@ C @@ ::= 2;", macro));
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << macro->Dump();
    ASSERT_NO_THROW(Parse("@__OPTION_POP__;", macro));

    // после pop настройка восстановлена (Fatal снова активен)
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("@@ D @@ ::= 1;", macro));
    ASSERT_ANY_THROW(Parse("@@ D @@ ::= 2;", macro)) << macro->Dump();
}

TEST_F(MacroTest, OptionUnknownOptionFatal) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_ANY_THROW(Parse("@__OPTION__(\"unknown-option\", \"ignore\");", macro));
}

TEST_F(MacroTest, OptionUnknownSeverityFatal) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    ASSERT_ANY_THROW(Parse("@__OPTION__(\"macro-redefined\", \"bogus\");", macro));
}

// Вложенный Parser (через Parser::ParseTerm) наследует Macro из Context:
// dsl-макрос, загруженный в m_ctx, раскрывается во вложенном парсере.
// macro_expand=false — специальный случай «без раскрытия макросов».
TEST_F(MacroTest, NestedParserInheritsMacroFromContext) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    ASSERT_NO_THROW(Parse("@@ true @@ := 1;", macro));

    TermPtr term = Parser::ParseTerm("@true;", m_ctx);
    ASSERT_TRUE(term);
    ASSERT_EQ(TermID::INTEGER, term->getTermID()) << term->toString();
    ASSERT_EQ("1", term->getText());

    // macro_expand=false — раскрытие отключено, остаётся токен MACRO.
    term = Parser::ParseTerm("@true;", m_ctx, true, /*macro_expand=*/false);
    ASSERT_TRUE(term);
    ASSERT_EQ(TermID::MACRO, term->getTermID()) << term->toString();
}

// ══════════════════════════════════════════════════════════════
//  Source-map маппинг макросов (addMacroMapping при раскрытии)
//  Проверяет, что bodyRange вызова макроса покрывает ровно токены
//  вызова (а не следующий за ним токен), а defRange указывает на
//  тело макроса.
// ══════════════════════════════════════════════════════════════

namespace {

// 0-based индекс в строке → 1-based offset (используется makeLoc/getText)
uint32_t off1(size_t idx0) {
    return static_cast<uint32_t>(idx0 + 1);
}

} // namespace

// Определение `@@alias@@ := replace;`, затем вызов `alias;`.
// Запрос на позиции вызова должен дать определение макроса (тело `replace`).
TEST_F(MacroTest, MacroMapping_SimpleCall_MapsToBody) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    std::string src = "@@alias@@ := replace; alias;";
    ASSERT_NO_THROW(Parse(src, macro));

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rFile = reader->findFileIdx("@input");
    ASSERT_FALSE(rFile.isInvalid());
    MapperFile mFile = m_ctx.source().findFileIdx("@input");
    ASSERT_FALSE(mFile.isInvalid());

    size_t defIdx = src.find("replace");
    size_t callIdx = src.rfind("alias");
    ASSERT_NE(defIdx, std::string::npos);
    ASSERT_NE(callIdx, std::string::npos);
    ASSERT_GT(callIdx, defIdx); // определение макроса расположено до вызова

    auto atCall = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(callIdx))));
    ASSERT_TRUE(atCall.has_value());

    // defRange указывает на тело макроса `replace`
    EXPECT_EQ(reader->getText(*atCall), "replace");
    EXPECT_EQ(atCall->begin.fileIdx(), rFile);

    // Позиция внутри последнего символа вызова тоже отображается (delta-проекция)
    auto inCall = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(callIdx + 4))));
    ASSERT_TRUE(inCall.has_value());
}

// Определение `@@foo($a)@@ := @@bar(@$a)@@;`, вызов `foo(1);`.
// Весь вызов (включая скобки и аргумент) отображается на тело макроса.
// Также проверяет, что ссылка на аргумент @$a в теле корректно резолвится.
TEST_F(MacroTest, MacroMapping_CallWithArgs_MapsWholeCall) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    std::string src = "@@foo($a)@@ := @@bar(@$a)@@; foo(1);";
    ASSERT_NO_THROW(Parse(src, macro));

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rFile = reader->findFileIdx("@input");
    ASSERT_FALSE(rFile.isInvalid());
    MapperFile mFile = m_ctx.source().findFileIdx("@input");
    ASSERT_FALSE(mFile.isInvalid());

    size_t callIdx = src.find("foo(1)");
    ASSERT_NE(callIdx, std::string::npos);

    auto atCall = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(callIdx))));
    ASSERT_TRUE(atCall.has_value());
    EXPECT_EQ(reader->getText(*atCall), "bar(@$a)");

    // Позиция внутри аргументов вызова (открывающая скобка) тоже в пределах вызова
    size_t argIdx = src.find('(', callIdx);
    ASSERT_NE(argIdx, std::string::npos);
    auto inArgs = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(argIdx))));
    ASSERT_TRUE(inArgs.has_value());
}

// Регрессия на off-by-one: раньше bodyRange захватывал следующий за вызовом
// токен. При inline-вызове `alias + 1` позиция оператора `+` (строго за концом
// вызова) НЕ должна отображаться в определение макроса.
TEST_F(MacroTest, MacroMapping_DoesNotSwallowNextToken) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);

    std::string src = "@@alias@@ := replace; x := alias + 1;";
    ASSERT_NO_THROW(Parse(src, macro));

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    ReaderFile rFile = reader->findFileIdx("@input");
    ASSERT_FALSE(rFile.isInvalid());
    MapperFile mFile = m_ctx.source().findFileIdx("@input");
    ASSERT_FALSE(mFile.isInvalid());

    size_t plusIdx = src.find('+');
    ASSERT_NE(plusIdx, std::string::npos);

    // Позиция на операторе '+' находится после конца вызова `alias` — маппинга быть не должно
    auto atPlus = reader->getMacroDefRange(static_cast<ReaderLocation>(m_ctx.source().makeLoc(mFile, off1(plusIdx))));
    EXPECT_FALSE(atPlus.has_value());
}
