#include "syntax/parser_test_fixture.hpp"
TEST_F(ParserTest, Docs) {
    ASSERT_TRUE(Parse("/** doc */ { }"));
    ASSERT_TRUE(Parse("/// \n{ }"));
    ASSERT_TRUE(Parse("{ /// doc\n }"));

    ASSERT_TRUE(Parse("/** doc\n\n */\n value := { };"));
    ASSERT_TRUE(Parse("/// doc1 \n/// doc2\n value := { };"));
    ASSERT_TRUE(Parse("value := 100; ///< doc"));
    ASSERT_TRUE(Parse("value := 100; ///< doc\n"));
}

TEST_F(ParserTest, HelloWorld) {
    ASSERT_TRUE(Parse("hello(str=\"\") := { printf(format:FmtChar, ...):Int32 := Pointer('printf'); printf('%s', $1); $str;};"));
    //    ASSERT_EQ("!!!!!!!!!!!!!!", ast->toString());
}

TEST_F(ParserTest, EnumTypeDecl) {
    // Enum через :Enum(...) / (...):Enum (как Tuple) - DictLiteral с аннотацией Enum.
    EXPECT_TRUE(Parse("Color ::= :Enum(RED, GREEN);"));
    EXPECT_TRUE(Parse("Color ::= (RED, GREEN,):Enum;"));
    EXPECT_TRUE(Parse("Color ::= :Enum(RED=1, GREEN=2);"));
    EXPECT_TRUE(Parse("Color ::= (RED=1, GREEN=2,):Enum;"));
    EXPECT_TRUE(Parse("Color ::= :Enum(RED:Int64=0, GREEN);"));
    EXPECT_TRUE(Parse("Color ::= (RED:Int64=0, GREEN,):Enum;"));
}

TEST_F(ParserTest, Closure) {
    EXPECT_TRUE(Parse("closure() ::= [](){};"));
    EXPECT_TRUE(Parse("closure() ::= [...](){};"));
    EXPECT_TRUE(Parse("closure() ::= [name](){};"));
    EXPECT_TRUE(Parse("closure() ::= [*name, &name](){};"));

    EXPECT_TRUE(Parse("closure() ::= [](): Int32 {};"));
    EXPECT_TRUE(Parse("closure() ::= [...](): Int32 {};"));
    EXPECT_TRUE(Parse("closure() ::= [... :Int32](): Int32 {};"));
    EXPECT_TRUE(Parse("closure() ::= [name](): Int32 {};"));
    EXPECT_TRUE(Parse("closure() ::= [*name, &name](): Int32 {};"));

    EXPECT_TRUE(Parse("closure(): Int32 ::= [](arg){};"));
    EXPECT_TRUE(Parse("closure(): Int32 ::= [...](arg:Int32, arg2){};"));
    EXPECT_TRUE(Parse("closure(): Int32 ::= [name](arg, arg2=0){};"));
    EXPECT_TRUE(Parse("closure(): Int32 ::= [*name, &name](arg, arg2:Int32=0){};"));

    EXPECT_TRUE(Parse("closure(): Int32 ::= [](arg^):Int32 {};"));
    EXPECT_TRUE(Parse("closure(): Int32 ::= [...](arg^:Int32, arg2^):Int32 {};"));
    EXPECT_TRUE(Parse("closure(): Int32 ::= [... :Int32](arg^:Int32, arg2^):Int32 {};"));
    EXPECT_TRUE(Parse("closure(): Int32 ::= [name](arg, arg2=0) :Int32 {};"));
    EXPECT_TRUE(Parse("closure(): Int32 ::= [*name, &name](arg, arg2:Int32=0) :Int32 {};"));
}

// TEST_F(ParserTest, Attribute) {
//     EXPECT_TRUE(Parse("[@ attr @] closure() ::= [](){};"));
//     EXPECT_TRUE(Parse("[@ attr1() @] func() ::= {};"));
//     EXPECT_TRUE(Parse("[@ attr1('str') @] func() ::= {};"));
//     EXPECT_TRUE(Parse("[@ attr1(name='str') @] func() ::= {};"));
//     EXPECT_TRUE(Parse("[@ attr1('str', 0) @] func() ::= {};"));
//     EXPECT_TRUE(Parse("[@ attr1(name='str', name2=0) @] func() ::= {};"));

//     EXPECT_TRUE(Parse("[@ attr1 @] [@ attr2 @] closure() ::= [](){};"));
//     EXPECT_TRUE(Parse("[@ attr1() @] [@ attr2 @] [@ attr3 @] func() ::= {};"));
//     EXPECT_TRUE(Parse("[@ attr1('str') @] [@attr2@] func() ::= {};"));
// }

TEST_F(ParserTest, Class) {
    EXPECT_TRUE(Parse(":Name := :Class(){};"));
    EXPECT_TRUE(Parse(":Name := :_(){ filed1 = 1; };"));
    EXPECT_TRUE(Parse(":Name := ns::Class(){ filed1 := 1; filed1 ::= 2; };"));
    EXPECT_TRUE(Parse(":Name := ::(){ func = {};};"));
    EXPECT_TRUE(Parse(":Name := :Class(){ func1 := {}; func2 ::= {};};"));
    EXPECT_TRUE(Parse("Name := Class(){ func() = {};};"));
    EXPECT_TRUE(Parse("::Name() := ::Func(){ func1() := {}; func2(arg) ::= {};};"));
    EXPECT_TRUE(Parse(":Name := ::Class(){ func() = {};};"));
    EXPECT_TRUE(Parse(":Name := :Class(args) { func1() := {}; func2(arg) ::= {};};"));

    EXPECT_TRUE(Parse("Name := :Class(){};"));
    EXPECT_TRUE(Parse("Name := :_(){ filed1 = 1; };"));
    EXPECT_TRUE(Parse("::ns::Name := ns::Class(){ filed1 := 1; filed1 ::= 2; };"));
    EXPECT_TRUE(Parse("Name::ns := ::(){ func = {};};"));
    EXPECT_TRUE(Parse("Name::ns := :Class(){ func1 := {}; func2 ::= {};};"));
    EXPECT_TRUE(Parse("::Name := Class(){ func() = {};};"));
    EXPECT_TRUE(Parse("::Name() := ::Func(){ func1() := {}; .func2(arg) ::= {};};"));
    EXPECT_TRUE(Parse("::Name := ::Class(){ func() = {};};"));
    EXPECT_TRUE(Parse("::Name := :Class(args) { func1() := {}; func2(arg) ::= {};};"));

    EXPECT_TRUE(Parse("Name() := :Class(), Class(){};"));
    EXPECT_TRUE(Parse("Name() := :_(), Class(), Class(){ .filed1 = 1; };"));
    EXPECT_TRUE(Parse("::ns::Name() := ns::Class(), Class(), Class(){ filed1 := 1; filed1 ::= 2; };"));
    EXPECT_TRUE(Parse("Name::ns() := ::(), Class(){ func = {};};"));
    EXPECT_TRUE(Parse("Name::ns() := :Class(), Class(), Class(), Class(){ func1 := {}; func2 ::= {};};"));
    EXPECT_TRUE(Parse("::Name() := Class(), Class(), Class(), Class(){ func() = {};};"));
    EXPECT_TRUE(Parse("{ ::Name() := ::Func(), ns::ns::Class(){ func1() := {}; func2(arg) ::= {};}; }"));
    EXPECT_TRUE(Parse("ns { ::Name() := ::Class(), Class(){ func() = {};}; }"));
    EXPECT_TRUE(Parse("ns::ns{ ::Name() := Class(args), ns::Class(), ::ns::Class() { :func1() := {}; func2(arg) ::= {};};}"));

    EXPECT_TRUE(Parse("ns::ns::Name:: :func1()"));
    EXPECT_TRUE(Parse(":: :func1"));
    EXPECT_TRUE(Parse("::ns::ns::Name:: :func1 := {}"));
}

TEST_F(ParserTest, Module) {
    ASSERT_TRUE(Parse("\\module(func)"));
    ASSERT_TRUE(Parse("\\\\dir\\mod_ule(func)"));
    ASSERT_TRUE(Parse("\\dir\\dir\\mod__ule(func)"));

    //    ASSERT_TRUE(Parse("\\module (*)"));
    //    ASSERT_TRUE(Parse("\\dir\\module (*)"));
    //    ASSERT_TRUE(Parse("\\\\dir\\dir\\module (*)"));
    //
    //    ASSERT_TRUE(Parse("\\\\module (func, func2)"));
    //    ASSERT_TRUE(Parse("\\dir\\module (func, *)"));
    //    ASSERT_TRUE(Parse("\\dir\\dir\\module (func, _)"));
    //
    //    ASSERT_TRUE(Parse("\\module (func, ::func2)"));
    //    ASSERT_TRUE(Parse("\\dir\\module (ns::func, *)"));
    //    ASSERT_TRUE(Parse("\\dir\\dir\\module (::ns::func, _)"));

    ASSERT_TRUE(Parse("\\module (name=func, name=func2, name=::func3)"));
    ASSERT_TRUE(Parse("\\\\dir\\module (name=ns::func, name='')"));
    ASSERT_TRUE(Parse("\\dir\\dir\\module (name=::ns::func, name=_)"));

    ASSERT_TRUE(Parse("\\module::var"));
    ASSERT_TRUE(Parse("\\module::ns::var"));
    ASSERT_TRUE(Parse("\\\\module::ns::func()"));

    ASSERT_TRUE(Parse("\\\\dir\\module::var"));
    ASSERT_TRUE(Parse("\\dir\\dir\\module::ns::var"));
    ASSERT_TRUE(Parse("\\dir\\dir\\dir\\module::ns::func()"));

    //    ASSERT_TRUE(Parse("\\module (name=func, name=::name::*)"));
    //    ASSERT_TRUE(Parse("\\dir.module (name=ns::name::*, name=*)"));

    ASSERT_FALSE(CheckCharModuleName("\\_module"));
    ASSERT_FALSE(CheckCharModuleName("\\\\_module"));
    ASSERT_FALSE(CheckCharModuleName("\\module_"));
    ASSERT_FALSE(CheckCharModuleName("\\\\module_"));

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("\\_module"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("\\\\_module"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("\\module_"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("\\\\module_"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(ParserTest, SkipBrackets) {

    Macro macro(m_ctx);
    SequenceType buffer;

    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 0));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 1));

    buffer.push_back(Term::Create(TermID::NAME, "name", {}, parser::token_type::NAME));

    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 0));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 1));

    buffer.push_back(Term::Create(TermID::LPAREN, "(", {}, parser::token_type::LPAREN));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 0));
    ASSERT_ANY_THROW(Parser::SkipBrackets(buffer, 1));

    buffer.push_back(Term::Create(TermID::RPAREN, ")", {}, parser::token_type::RPAREN));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 0));
    ASSERT_EQ(2, Parser::SkipBrackets(buffer, 1));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 2));

    buffer.insert(buffer.begin(), Term::Create(TermID::NAME, "first", {}, parser::token_type::NAME)); // first name ( )

    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 0));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 1));

    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 0));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 1));
    ASSERT_EQ(2, Parser::SkipBrackets(buffer, 2));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 3));

    buffer.insert(buffer.end() - 1, Term::Create(TermID::ELLIPSIS, "...", {}, parser::token_type::ELLIPSIS));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 0));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 1));
    ASSERT_EQ(3, Parser::SkipBrackets(buffer, 2));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 3));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 4));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 5));

    buffer.insert(buffer.end() - 1, Term::Create(TermID::NAME, "name", {}, parser::token_type::NAME));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 0));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 1));
    ASSERT_EQ(4, Parser::SkipBrackets(buffer, 2));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 3));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 4));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 5));
    ASSERT_EQ(0, Parser::SkipBrackets(buffer, 6));
}

TEST_F(ParserTest, MacroCreate) {
    MacroPtr macro = std::make_shared<Macro>(m_ctx);
    TermPtr term;
    ASSERT_NO_THROW(term = Parse("@@ name @@ macro @@@@;", macro));
    ASSERT_TRUE(term);
    ASSERT_TRUE(term->m_left) << term->toString();
    ASSERT_TRUE(term->m_right) << term->toString();

    // Повторное определение той же сигнатуры (в т.ч. внутри блока) - по умолчанию предупреждение.
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("{ @@ name @@ macro } @@@@;", macro));
    EXPECT_NO_THROW(Parse("{+ @@ name @@ macro +} @@@@;", macro));
}

TEST_F(ParserTest, DISABLED_Convert) {
    std::vector<const char*> list = {
        "brother(human!, human!)?;",
        "func(arg1, arg2 = 5) :- { ($arg1 < $2) -> {% return $arg1; %}, [...] --> {% return *$1 * *$2; %}; };",
        "func_sum(arg1, arg2) :- {$arg1 + $arg2;};",
    };
    Parser parser(m_ctx);
    for (size_t i = 0; i < list.size(); i++) {
        ASSERT_NO_THROW(parser.ParseText(list[i]);) << "FROM: " << list[i];
        std::string to_str = parser.GetAst()->toString() + ";";
        ASSERT_NO_THROW(parser.ParseText(to_str);) << "CONVERT: " << to_str;
    }
}

// ===== Покрытие аргументов функций в определениях и вызовах =====

TEST_F(ParserTest, FuncAssignOpsEq) {
    ASSERT_TRUE(Parse("func(arg) = { };"));
    ASSERT_EQ(TermID::ASSIGN, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg)={};", ast->toString());

    ASSERT_TRUE(Parse("func(arg1, arg2=2) = { };"));
    ASSERT_EQ(TermID::ASSIGN, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg1, arg2=2)={};", ast->toString());

    ASSERT_TRUE(Parse("func(arg:Int32=1) = { };"));
    ASSERT_EQ(TermID::ASSIGN, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg:Int32=1)={};", ast->toString());

    ASSERT_EQ(1, ast->m_left->size());
    ASSERT_EQ("arg", ast->m_left->at(0).first);
    ASSERT_EQ("1", ast->m_left->at(0).second->m_right->getText());
    // НОРМАЛИЗАЦИЯ: тип аргумента - единый слот m_type ARGUMENT-терма.
    ASSERT_TRUE(ast->m_left->at(0).second->m_type);
}

TEST_F(ParserTest, FuncAssignOpsCreateUse) {
    ASSERT_TRUE(Parse("func(arg) := { };"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg) := {};", ast->toString());

    ASSERT_TRUE(Parse("func(arg1:Int32=1, arg2) := { };"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg1:Int32=1, arg2) := {};", ast->toString());

    ASSERT_EQ(2, ast->m_left->size());
    ASSERT_EQ("arg1", ast->m_left->at(0).first);
    ASSERT_EQ("1", ast->m_left->at(0).second->m_right->getText());
    // НОРМАЛИЗАЦИЯ: тип аргумента - единый слот m_type ARGUMENT-терма.
    ASSERT_TRUE(ast->m_left->at(0).second->m_type);
    ASSERT_EQ("arg2", ast->m_left->at(1).second->getText());
}

TEST_F(ParserTest, FuncAssignOpsCreateNew) {
    ASSERT_TRUE(Parse("func(arg) ::= { };"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg) ::= {};", ast->toString());

    ASSERT_TRUE(Parse("func(arg1, arg2=2) ::= { };"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg1, arg2=2) ::= {};", ast->toString());

    ASSERT_TRUE(Parse("func(arg:Int32=1, arg2) ::= { };"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg:Int32=1, arg2) ::= {};", ast->toString());
}

TEST_F(ParserTest, FuncAssignOpsAppend) {
    ASSERT_TRUE(Parse("func(arg) []= { };"));
    ASSERT_EQ(TermID::APPEND, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg) []= {};", ast->toString());

    ASSERT_TRUE(Parse("func(arg1:Int32=1, arg2) []= { };"));
    ASSERT_EQ(TermID::APPEND, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg1:Int32=1, arg2) []= {};", ast->toString());
}

TEST_F(ParserTest, CallArgTypePositional) {
    ASSERT_TRUE(Parse("term(arg:Int32);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("arg:Int32", ast->at(0).second->toString());
    ASSERT_TRUE(ast->at(0).second->m_type);
    ASSERT_EQ(":Int32", ast->at(0).second->m_type->getText());

    ASSERT_TRUE(Parse("term(name:Int32=1);"));
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    ASSERT_EQ("1", ast->at(0).second->m_right->getText());
    // НОРМАЛИЗАЦИЯ: тип аргумента - единый слот m_type ARGUMENT-терма.
    ASSERT_TRUE(ast->at(0).second->m_type);
    ASSERT_EQ(":Int32", ast->at(0).second->m_type->getText());
    ASSERT_EQ("term(name:Int32=1)", ast->toString());
}

TEST_F(ParserTest, CallArgTypeMixed) {
    ASSERT_TRUE(Parse("term(name=1, arg);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    ASSERT_EQ("1", ast->at(0).second->m_right->getText());
    ASSERT_EQ("arg", ast->at(1).second->getText());
    ASSERT_EQ("term(name=1, arg)", ast->toString());

    ASSERT_TRUE(Parse("term(arg:Int32, name=1);"));
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("arg", ast->at(0).second->getText());
    ASSERT_TRUE(ast->at(0).second->m_type);
    ASSERT_EQ(":Int32", ast->at(0).second->m_type->getText());
    ASSERT_EQ("name", ast->at(1).first);
    ASSERT_EQ("1", ast->at(1).second->m_right->getText());
    ASSERT_EQ("term(arg:Int32, name=1)", ast->toString());

    ASSERT_TRUE(Parse("term(name:Int32=1, arg2:Int64=2);"));
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    ASSERT_EQ("1", ast->at(0).second->m_right->getText());
    // НОРМАЛИЗАЦИЯ: тип аргумента - единый слот m_type ARGUMENT-терма.
    ASSERT_TRUE(ast->at(0).second->m_type);
    ASSERT_EQ(":Int32", ast->at(0).second->m_type->getText());
    ASSERT_EQ("arg2", ast->at(1).first);
    ASSERT_EQ("2", ast->at(1).second->m_right->getText());
    // НОРМАЛИЗАЦИЯ: тип аргумента - единый слот m_type ARGUMENT-терма.
    ASSERT_TRUE(ast->at(1).second->m_type);
    ASSERT_EQ(":Int64", ast->at(1).second->m_type->getText());
    ASSERT_EQ("term(name:Int32=1, arg2:Int64=2)", ast->toString());
}

TEST_F(ParserTest, CallArgOps) {
    // ARGUMENT-обёртка: m_left=имя, m_right=значение
    ASSERT_TRUE(Parse("term(name=1+2);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    ASSERT_EQ(TermID::ARGUMENT, ast->at(0).second->getTermID());
    ASSERT_EQ("name", ast->at(0).second->m_left->getText());
    TermPtr op = ast->at(0).second->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ(TermID::OP_MATH, op->getTermID());
    ASSERT_EQ("+", op->getText());
    ASSERT_TRUE(op->m_left);
    ASSERT_TRUE(op->m_right);
    ASSERT_EQ("1", op->m_left->getText());
    ASSERT_EQ("2", op->m_right->getText());

    ASSERT_TRUE(Parse("term(name=(1+2)*3);"));
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    op = ast->at(0).second->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("*", op->getText());
    ASSERT_TRUE(op->m_left);
    ASSERT_TRUE(op->m_right);
    ASSERT_EQ("+", op->m_left->getText());

    ASSERT_TRUE(Parse("term(name=call(1+2));"));
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    op = ast->at(0).second->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("call", op->getText());
}

TEST_F(ParserTest, CallArgEllipsis) {
    ASSERT_TRUE(Parse("term(...arg);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("term(...arg)", ast->toString());

    ASSERT_TRUE(Parse("term(... ...arg);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("term(... ...arg)", ast->toString());

    ASSERT_TRUE(Parse("term(...arg...);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("term(...arg...)", ast->toString());

    ASSERT_TRUE(Parse("term(name=1, ...args);"));
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    ASSERT_EQ("1", ast->at(0).second->m_right->getText());
    ASSERT_EQ("term(name=1, ...args)", ast->toString());

    ASSERT_TRUE(Parse("term(...args, name=1);"));
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("name", ast->at(1).first);
    ASSERT_EQ("1", ast->at(1).second->m_right->getText());
    ASSERT_EQ("term(...args, name=1)", ast->toString());
}

TEST_F(ParserTest, CallArgRefType) {
    ASSERT_TRUE(Parse("term(&arg:Int32);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(1, ast->size());
    // Узел-оператор & оборачивает arg:Int32.
    ASSERT_EQ(TermID::OPERATOR_PTR, ast->at(0).second->getTermID()) << trust::toString(ast->at(0).second->getTermID());
    ASSERT_EQ("&", ast->at(0).second->getText());
    ASSERT_TRUE(ast->at(0).second->m_right);
    ASSERT_EQ("arg", ast->at(0).second->m_right->getText());
    ASSERT_TRUE(ast->at(0).second->m_right->m_type);
    ASSERT_EQ(":Int32", ast->at(0).second->m_right->m_type->getText());

    ASSERT_TRUE(Parse("term(&arg:Int32=val);"));
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("arg", ast->at(0).first);
    // ARGUMENT.m_right - узел-оператор &, его m_right - val; тип - единый слот m_type ARGUMENT-терма.
    ASSERT_EQ(TermID::OPERATOR_PTR, ast->at(0).second->m_right->getTermID()) << trust::toString(ast->at(0).second->m_right->getTermID());
    ASSERT_TRUE(ast->at(0).second->m_right->m_right);
    ASSERT_EQ("val", ast->at(0).second->m_right->m_right->getText());
    ASSERT_TRUE(ast->at(0).second->m_type);

    ASSERT_TRUE(Parse("term(*arg:Int32);"));
    ASSERT_EQ(1, ast->size());
    // take (*) - оператор; оборачивает arg:Int32.
    ASSERT_EQ(TermID::TAKE, ast->at(0).second->getTermID());
    ASSERT_EQ("*", ast->at(0).second->getText());
    ASSERT_TRUE(ast->at(0).second->m_right);
    ASSERT_EQ("arg", ast->at(0).second->m_right->getText());
    ASSERT_TRUE(ast->at(0).second->m_right->m_type);

    ASSERT_TRUE(Parse("term(&*arg:Int32=val);"));
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("arg", ast->at(0).first);
    // &* - узел-оператор OPERATOR_PTR (&*), оборачивает val; тип - единый слот m_type ARGUMENT-терма.
    ASSERT_EQ(TermID::OPERATOR_PTR, ast->at(0).second->m_right->getTermID()) << trust::toString(ast->at(0).second->m_right->getTermID());
    ASSERT_EQ("&*", ast->at(0).second->m_right->getText());
    ASSERT_TRUE(ast->at(0).second->m_right->m_right);
    ASSERT_EQ("val", ast->at(0).second->m_right->m_right->getText());
    ASSERT_TRUE(ast->at(0).second->m_type);
}

TEST_F(ParserTest, CallArgStringName) {
    ASSERT_TRUE(Parse("term('name'=1);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    ASSERT_EQ("1", ast->at(0).second->m_right->getText());
    ASSERT_EQ("term(name=1)", ast->toString());
}

TEST_F(ParserTest, FuncFullCombinations) {
    ASSERT_TRUE(Parse("func(a:Int32=1, b, c:Int64=2, ...) := { };"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(a:Int32=1, b, c:Int64=2, ...) := {};", ast->toString());
    ASSERT_EQ(4, ast->m_left->size());

    ASSERT_TRUE(Parse("func(a:Int32=val, b, c=2) ::= { };"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(a:Int32=val, b, c=2) ::= {};", ast->toString());
    ASSERT_EQ(3, ast->m_left->size());
}

// ===== Прямые тесты для FinalizeAndTest =====

TEST_F(ParserTest, FinalizeAndTestNoLeft) {
    // Вызов без m_left - только установка id
    auto term = Term::Create(TermID::NAME, "name", {}, parser::token_type::NAME);
    EXPECT_EQ(TermID::NAME, term->m_id);
    EXPECT_FALSE(term->m_left);

    term->FinalizeAndTest(TermID::STATIC);
    EXPECT_EQ(TermID::STATIC, term->m_id);
    EXPECT_EQ("name", term->getText());
    EXPECT_FALSE(term->m_left);
}

TEST_F(ParserTest, FinalizeAndTestNoLeftConst) {
    // Вызов без m_left с константным именем (заканчивается на ^)
    auto term = Term::Create(TermID::NAME, "const^", {}, parser::token_type::NAME);
    EXPECT_EQ("const^", term->getText());

    term->FinalizeAndTest(TermID::STATIC);
    EXPECT_EQ(TermID::STATIC, term->m_id);
    EXPECT_EQ("const^", term->getText()); // ^ сохраняется в имени
}

TEST_F(ParserTest, FinalizeAndTestTextJoin) {
    // Склейка текста из цепочки m_left
    // Создаём термы: name2 -> name1 -> term
    // где name2.m_left = name1, name1.m_left = term
    // После FinalizeAndTest m_text должен быть "termname1name2"
    // (insert(0, ...) - самый левый элемент оказывается в начале)
    auto name2 = Term::Create(TermID::NAME, "name2", {}, parser::token_type::NAME);
    auto name1 = Term::Create(TermID::NAME, "name1", {}, parser::token_type::NAME);
    auto term = Term::Create(TermID::NAME, "term", {}, parser::token_type::NAME);

    name2->m_left = name1;
    name1->m_left = term;

    EXPECT_TRUE(name2->m_left);
    EXPECT_EQ("name2", name2->getText());

    name2->FinalizeAndTest(TermID::STATIC);
    EXPECT_EQ(TermID::STATIC, name2->m_id);
    EXPECT_EQ("termname1name2", name2->getText());
    EXPECT_FALSE(name2->m_left); // m_left должен быть очищен
}

TEST_F(ParserTest, FinalizeAndTestTextJoinChain) {
    // Склейка текста из цепочки длиной 3: c -> b -> a
    // После FinalizeAndTest: m_text = "abc"
    auto c = Term::Create(TermID::NAME, "c", {}, parser::token_type::NAME);
    auto b = Term::Create(TermID::NAME, "b", {}, parser::token_type::NAME);
    auto a = Term::Create(TermID::NAME, "a", {}, parser::token_type::NAME);

    c->m_left = b;
    b->m_left = a;

    EXPECT_TRUE(c->m_left);
    EXPECT_EQ("c", c->getText());

    c->FinalizeAndTest(TermID::NATIVE);
    EXPECT_EQ(TermID::NATIVE, c->m_id);
    EXPECT_EQ("abc", c->getText());
    EXPECT_FALSE(c->m_left);

    // Проверяем, что m_left у b и a тоже сброшены
    EXPECT_FALSE(b->m_left);
    EXPECT_FALSE(a->m_left);
}

TEST_F(ParserTest, FinalizeAndTestWithNamespace) {
    // Моделируем ситуацию из grammar: ns_part ::= ns_part NAMESPACE NAME
    // Создаём цепочку: name2 -> NAMESPACE("::") -> name1
    // После FinalizeAndTest: m_text должен быть "name1::name2"
    // (insert(0, ...) - самый левый элемент в начале)
    auto name2 = Term::Create(TermID::NAME, "name2", {}, parser::token_type::NAME);
    auto ns = Term::Create(TermID::NAMESPACE, "::", {}, parser::token_type::NAMESPACE);
    auto name1 = Term::Create(TermID::NAME, "name1", {}, parser::token_type::NAME);

    name2->AppendLeft(ns);
    ns->AppendLeft(name1);

    EXPECT_TRUE(name2->m_left);
    EXPECT_EQ("name2", name2->getText());

    name2->FinalizeAndTest(TermID::STATIC);
    EXPECT_EQ(TermID::STATIC, name2->m_id);
    EXPECT_EQ("name1::name2", name2->getText());
    EXPECT_FALSE(name2->m_left);
}

// -- Конвертер Term→AstNode НЕ мутирует исходный Term (range-оверрайды вместо мутации). --
TEST_F(ParserTest, ConverterDoesNotMutateTerm) {
    MapperFile f = m_ctx.source().add_source("t.src", "x := 1;", true);
    MapperRange nameRange(m_ctx.source().makeLoc(f, 1), m_ctx.source().makeLoc(f, 2)); // "x"
    MapperRange opRange(m_ctx.source().makeLoc(f, 4), m_ctx.source().makeLoc(f, 4));   // ":="
    MapperRange litRange(m_ctx.source().makeLoc(f, 6), m_ctx.source().makeLoc(f, 7));  // "1"

    auto name = Term::Create(TermID::NAME, "x", nameRange, parser::token_type::END);
    auto lit = Term::Create(TermID::INTEGER, "1", litRange, parser::token_type::END);
    auto cn = Term::Create(TermID::CREATE_NAME, ":=", opRange, parser::token_type::END);
    cn->m_left = name;
    cn->m_right = lit;

    // Снапшоты range до конвертации.
    const auto nameBefore = name->m_mapperRange;
    const auto litBefore = lit->m_mapperRange;
    const auto cnBefore = cn->m_mapperRange;

    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(cn, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);

    // Термы НЕ мутированы (прежний код расширял term->m_mapperRange оператора до [имя, expr]).
    ASSERT_EQ(nameBefore.begin.offset(), name->m_mapperRange.begin.offset());
    ASSERT_EQ(nameBefore.end.offset(), name->m_mapperRange.end.offset());
    ASSERT_EQ(litBefore.begin.offset(), lit->m_mapperRange.begin.offset());
    ASSERT_EQ(litBefore.end.offset(), lit->m_mapperRange.end.offset());
    ASSERT_EQ(cnBefore.begin.offset(), cn->m_mapperRange.begin.offset());
    ASSERT_EQ(cnBefore.end.offset(), cn->m_mapperRange.end.offset());

    // Диапазон узла - расширенный [имя, expr], вычисляется на лету в VarDecl::range().
    ASSERT_EQ(nameRange.begin.offset(), vd->range().begin.offset()) << "VarDecl range begins at name";
    ASSERT_EQ(litRange.end.offset(), vd->range().end.offset()) << "VarDecl range ends at initializer";
}

// Нереализованные TermID (без Kind) дают явную диагностику, а не тихий пустой узел.
// Ранее NAMESPACE/PARENT/ESCAPE вели в заглушку SemicolonStmt и молча пропадали из вывода.
TEST_F(ParserTest, UnimplementedTermsReportError) {
    for (const TermID id : {TermID::LBRACE, TermID::NAMESPACE, TermID::PARENT, TermID::ESCAPE}) {
        m_ctx.diag().clear();
        auto term = Term::Create(id, "x");
        std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(term, m_ctx);
        ASSERT_GT(m_ctx.diag().errorCount(), 0) << "TermID " << toString(id) << " must report an error, not silently drop";
        ASSERT_TRUE(nodes.empty()) << "TermID " << toString(id) << " must not produce a node";
    }
}

// Named-аргумент name=value → ArgNode получает расширенный range [имя, значение].
TEST_F(ParserTest, NamedArgumentRangeExpanded) {
    ASSERT_TRUE(Parse("f(a=1);"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* call = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(call);
    ASSERT_TRUE(call->m_args && call->m_args->size() == 1);
    auto* arg = dynamic_cast<ArgNode*>(call->m_args->at(0).get());
    ASSERT_TRUE(arg) << "named argument a=1 must convert to ArgNode";
    // range покрывает "a=1" (имя + оператор + значение).
    ASSERT_EQ("a=1", m_ctx.source().getText(arg->range())) << "named-arg range must cover name=value";
}
