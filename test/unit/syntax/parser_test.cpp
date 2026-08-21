
#include "syntax/warning_push.h"
#include <gtest/gtest.h>
#include "syntax/warning_pop.h"

#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token_type.hpp"
#include "ast/term_to_ast.hpp"
#include "syntax/parser.h"
#include "syntax/term.h"
#include "trust/version.h"
#include "syntax/macro.h"
#include "module_loader/module_loader.hpp"
#include <functional>

using namespace trust;

class ParserTest : public ::testing::Test {
  protected:
    trust::Context m_ctx;
    std::unique_ptr<ModuleLoader> m_loader;
    std::vector<std::string> m_postlex;

    std::string m_output;

    void SetUp() {
        m_ctx.diag().clear();
        // Парсер может обрабатывать import-модули через ctx.loader().
        m_loader = std::make_unique<ModuleLoader>(m_ctx);
        m_ctx.setLoader(m_loader.get());
    }

    void TearDown() {}

    TermPtr Parse(std::string str, MacroPtr buffer = nullptr) {
        m_postlex.clear();
        if (buffer) {
            m_ctx.setMacro(buffer);
        }
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

TEST_F(ParserTest, ParserParser) {
    ASSERT_TRUE(Parse("100"));
    ASSERT_EQ(TermID::INTEGER, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("100", ast->getText());

    ASSERT_TRUE(Parse("'str'"));
    ASSERT_EQ(TermID::STRCHAR, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("str", ast->getText());

    ASSERT_TRUE(Parse("name"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("name", ast->getText());

    ASSERT_TRUE(Parse("name = 1;"));
    ASSERT_EQ(TermID::ASSIGN, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("=", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_EQ("name", ast->m_left->getText());
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("1", ast->m_right->getText());
    ASSERT_EQ("name=1;", ast->toString());

    ASSERT_TRUE(Parse("name ::= '123';"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("::=", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_EQ("name", ast->m_left->getText());
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("123", ast->m_right->getText());
    ASSERT_EQ("name ::= '123';", ast->toString());

    ASSERT_TRUE(Parse("name ::= term;"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("::=", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_EQ("name", ast->m_left->getText());
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("term", ast->m_right->getText());
    ASSERT_EQ("name ::= term;", ast->toString());
}

TEST_F(ParserTest, LiteralInteger) {
    ASSERT_TRUE(Parse("100;"));
    ASSERT_EQ(TermID::INTEGER, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("100", ast->getText());
}

TEST_F(ParserTest, TermCreateStringView) {
    std::string source = "some_name";
    std::string_view sv(source.data(), source.size());
    // View-перегрузка: Create(id, lex_type, text, len) - НЕ копирует.
    TermPtr term = Term::Create(TermID::NAME, parser::token_type::NAME, sv.data(), sv.size());

    // m_text хранит string_view - данные не копируются, адрес совпадает с source
    const Term& cterm = *term;
    EXPECT_EQ(source.data(), cterm.getText().data());
    EXPECT_EQ(source.size(), cterm.getText().size());
    EXPECT_EQ("some_name", cterm.getText());

    // Не-const getText()/toString() материализует string_view в std::string
    EXPECT_EQ("some_name", term->toString());
    EXPECT_EQ("some_name", term->getText());
}

TEST_F(ParserTest, LiteralNumber) {
    ASSERT_TRUE(Parse("100.222;"));
    ASSERT_EQ(TermID::NUMBER, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("100.222", ast->getText());

    ASSERT_TRUE(Parse("1.2E-20;"));
    ASSERT_EQ(TermID::NUMBER, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("1.2E-20", ast->getText());

    ASSERT_TRUE(Parse("1.2E+20;"));
    ASSERT_EQ(TermID::NUMBER, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("1.2E+20", ast->getText());

    ASSERT_TRUE(Parse("0.e-10;"));
    ASSERT_EQ(TermID::NUMBER, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("0.e-10", ast->getText());

    ASSERT_TRUE(Parse("0.e+10;"));
    ASSERT_EQ(TermID::NUMBER, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("0.e+10", ast->getText());
}

TEST_F(ParserTest, LiteralString) {
    ASSERT_TRUE(Parse("\"\";"));
    ASSERT_EQ(TermID::STRWIDE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("\"\"", ast->toString());
}

TEST_F(ParserTest, LiteralString0) {
    ASSERT_TRUE(Parse("\"\"(123);"));
    ASSERT_TRUE(Parse("\"\"(123);"));
    ASSERT_EQ(TermID::STRWIDE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("\"\"(123)", ast->toString());
}

TEST_F(ParserTest, LiteralString1) {
    ASSERT_TRUE(Parse("\"\";"));
    ASSERT_EQ(TermID::STRWIDE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("\"\"", ast->toString());
}

TEST_F(ParserTest, LiteralString3) {
    ASSERT_TRUE(Parse("'';"));
    ASSERT_EQ(TermID::STRCHAR, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("''", ast->toString());
}

TEST_F(ParserTest, LiteralString4) {
    ASSERT_TRUE(Parse("'strbyte'(term(), 123);"));
    ASSERT_EQ(TermID::STRCHAR, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("'strbyte'(term(), 123)", ast->toString());
}

TEST_F(ParserTest, LiteralRational) {
    ASSERT_TRUE(Parse("1\\1;"));
    ASSERT_EQ(TermID::RATIONAL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("1\\1", ast->toString());

    /* Защита от случайной операции деления на единицу вместо указания дроби */
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("1/1"));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("rrr := 1/1"));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);
    ASSERT_NO_THROW(Parse("rrr := 1\\1"));
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("rrr := 11111111111111111/1"));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);
    ASSERT_NO_THROW(Parse("rrr := 11111111111111111\\1"));

    ASSERT_TRUE(Parse("100\\100;"));
    ASSERT_EQ(TermID::RATIONAL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("100\\100", ast->toString());

    ASSERT_TRUE(Parse("123456789123456789123456789\\123456789123456789123456789;"));
    ASSERT_EQ(TermID::RATIONAL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("123456789123456789123456789\\123456789123456789123456789", ast->toString());
}

TEST_F(ParserTest, DISABLED_LiteralComplex) {
    ASSERT_TRUE(Parse("1+0j;"));
    ASSERT_EQ(TermID::COMPLEX, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("1+0j", ast->toString());

    ASSERT_TRUE(Parse("1+0.1j;"));
    ASSERT_EQ(TermID::COMPLEX, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("1+0.1j", ast->toString());
}

TEST_F(ParserTest, TermSimple) {
    ASSERT_TRUE(Parse("term();"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->isCall());
    ASSERT_EQ("term", ast->getText());
}

TEST_F(ParserTest, InternalName) {
    ASSERT_TRUE(Parse("term.filed();"));
    // `.filed` - доступ по имени: FIELD-терм (объект в m_left, ключ в m_right);
    // `()` - вызов на поле.
    ASSERT_EQ(TermID::FIELD, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->m_left->getText());
    ASSERT_EQ("filed", ast->m_right->getText());
    ASSERT_EQ("term.filed()", ast->toString());
}

TEST_F(ParserTest, Tensor1) {
    ASSERT_TRUE(Parse("[,]:Int8"));
    ASSERT_EQ(TermID::TENSOR, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("[,]:Int8", ast->toString());

    ASSERT_TRUE(Parse("term[1];"));
    // `term[1]` - доступ по индексу: INDEX-терм (объект в m_left, индексы в m_args).
    ASSERT_EQ(TermID::INDEX, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->m_left->getText());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("1", ast->at(0).second->getText());

    ASSERT_TRUE(Parse("term[1..2];"));
    ASSERT_EQ(TermID::INDEX, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term[1..2]", ast->toString());

    //    ASSERT_TRUE(ast->m_right);
    //    ASSERT_EQ(TermID::INDEX, ast->m_right->getTermID()) << EnumStr(ast->getTermID());
    //    ASSERT_EQ(2, ast->m_right->getItemCount());
    //    ASSERT_EQ("1", ast->m_right->at(0)->getText());
    //    ASSERT_EQ("2", ast->m_right->at(1)->getText());
}

TEST_F(ParserTest, Tensor2) {
    ASSERT_TRUE(Parse("term[1, 2];"));
    // `term[1, 2]` - multi-индекс: INDEX-терм, все индексы в m_args.
    ASSERT_EQ(TermID::INDEX, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->m_left->getText());
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("1", ast->at(0).second->getText());
    ASSERT_EQ("2", ast->at(1).second->getText());

    ASSERT_TRUE(Parse("term[1, 1..2, 3];"));

    ASSERT_EQ(TermID::INDEX, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(3, ast->size());
    ASSERT_EQ("1", ast->at(0).second->getText());
    ASSERT_EQ("1..2", ast->at(1).second->toString());
    ASSERT_EQ("3", ast->at(2).second->getText());
}

TEST_F(ParserTest, Tensor3) {
    ASSERT_TRUE(Parse("term := [1,2,];"));

    ASSERT_TRUE(Parse("term[1, 3] := 0;"));

    ASSERT_TRUE(Parse("term[1, 3] := [[1,2,3,],];"));

    //    ASSERT_EQ(TermID::TERM, ast->getTermID()) << EnumStr(ast->getTermID());
    //    ASSERT_EQ("term", ast->getText());
    //    ASSERT_TRUE(ast->m_right);
    //
    //    ASSERT_EQ(TermID::INDEX, ast->m_right->getTermID()) << EnumStr(ast->getTermID());
    //    ASSERT_EQ(2, ast->m_right->size());
    //    ASSERT_EQ("1", ast->m_right->at(0)->getText());
    //    ASSERT_EQ("2", ast->m_right->at(1)->getText());
    //
    //    ASSERT_TRUE(Parse("term[1, 1..2, 3];"));
    //
    //    ASSERT_EQ(TermID::INDEX, ast->m_right->getTermID()) << EnumStr(ast->getTermID());
    //    ASSERT_EQ(3, ast->m_right->size());
    //    ASSERT_EQ("1", ast->m_right->at(0)->getText());
    //    ASSERT_EQ("1..2", ast->m_right->at(1)->toString());
    //    ASSERT_EQ("3", ast->m_right->at(2)->getText());
}

TEST_F(ParserTest, Tensor4) {
    ASSERT_TRUE(Parse(":Type( 1 );"));
    ASSERT_EQ(":Type(1)", ast->toString());

    ASSERT_TRUE(Parse(":Type(  [1,2,] )"));
    ASSERT_EQ(":Type([1, 2,])", ast->toString());

    ASSERT_TRUE(Parse(":Type( 1, 2 )"));
    ASSERT_EQ(":Type(1, 2)", ast->toString());

    ASSERT_TRUE(Parse(":Type(   name=1  ,   name2=2 )"));
    ASSERT_EQ(":Type(name=1, name2=2)", ast->toString());

    ASSERT_TRUE(Parse(":Type( \"str\" )"));
    ASSERT_EQ(":Type(\"str\")", ast->toString());

    ASSERT_TRUE(Parse(":Int32[3]( \"str\" ) "));
    ASSERT_EQ(":Int32[3](\"str\")", ast->toString());

    ASSERT_TRUE(Parse(":Int32[2,2](1,2,3,4);"));
    ASSERT_EQ(":Int32[2,2](1, 2, 3, 4)", ast->toString());

    ASSERT_TRUE(Parse(":Int32[2,2]( 0,   ...    )"));
    ASSERT_EQ(":Int32[2,2](0, ...)", ast->toString());

    ASSERT_TRUE(Parse(":Int32( ... ...  dict )"));
    ASSERT_EQ(":Int32(... ...dict)", ast->toString());

    ASSERT_TRUE(Parse(":Int32( ... dict )"));
    ASSERT_EQ(":Int32(...dict)", ast->toString());

    ASSERT_TRUE(Parse(":Int32[2,2](   ...   rand()  ...   )"));
    ASSERT_EQ(":Int32[2,2](...rand()...)", ast->toString());

    ASSERT_TRUE(Parse(":type[10]( 1,     2,  ...    rand()   ... )"));
    ASSERT_EQ(":type[10](1, 2, ...rand()...)", ast->toString());

    ASSERT_TRUE(Parse(":range( 0..100  )"));
    ASSERT_EQ(":range(0..100)", ast->toString());

    ASSERT_TRUE(Parse("range(  0 .. 100 .. 0.1 )"));
    ASSERT_EQ("range(0..100..0.1)", ast->toString());
}

/*
 * - Установка типов у литералов
 * - Проверка соответствия типов литералов и их значений
 *
 * - Встроенные функции преобразования простых типов данных
 * - Передача аргументов функций по ссылкам
 * - Проверка типов аргументов при вызове функций
 * - Проверка типов возвращаемых значений у функций
 * - Проверка типов у операторов присвоения
 */
TEST_F(ParserTest, ScalarType) {
    /*
     * - Установка типов у литералов
     * - Проверка соответствия типов литералов и их значений
     */

    ASSERT_TRUE(Parse("0;"));
    ASSERT_EQ("0", ast->toString());

    ASSERT_TRUE(Parse("1;"));
    ASSERT_EQ("1", ast->toString());

    ASSERT_TRUE(Parse("2;"));
    ASSERT_EQ("2", ast->toString());

    ASSERT_TRUE(Parse("2_2;"));
    ASSERT_EQ("2_2", ast->toString());

    ASSERT_TRUE(Parse("-1;"));
    ASSERT_EQ("-1", ast->toString());

    ASSERT_TRUE(Parse("256;"));
    ASSERT_EQ("256", ast->toString());

    ASSERT_TRUE(Parse("10_000;"));
    ASSERT_EQ("10_000", ast->toString());

    ASSERT_TRUE(Parse("100_000;"));
    ASSERT_EQ("100_000", ast->toString());

    ASSERT_TRUE(Parse("0.0;"));
    ASSERT_EQ("0.0", ast->toString());

    ASSERT_TRUE(Parse("0:Bool;"));
    ASSERT_EQ("0:Bool", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Bool", ast->m_type->getText());

    ASSERT_TRUE(Parse("0:Int32;"));
    ASSERT_EQ("0:Int32", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int32", ast->m_type->getText());

    ASSERT_TRUE(Parse("0:Int64;"));
    ASSERT_EQ("0:Int64", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int64", ast->m_type->getText());

    ASSERT_TRUE(Parse("0:Float32;"));
    ASSERT_EQ("0:Float32", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Float32", ast->m_type->getText());

    ASSERT_TRUE(Parse("0:Float64;"));
    ASSERT_EQ("0:Float64", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Float64", ast->m_type->getText());

    ASSERT_TRUE(Parse("1:Bool;"));
    ASSERT_EQ("1:Bool", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Bool", ast->m_type->getText());

    ASSERT_TRUE(Parse("1:Int8;"));
    ASSERT_EQ("1:Int8", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int8", ast->m_type->getText());

    ASSERT_TRUE(Parse("1:Float64;"));
    ASSERT_EQ("1:Float64", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Float64", ast->m_type->getText());

    ASSERT_TRUE(Parse("2:Int16;"));
    ASSERT_EQ("2:Int16", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int16", ast->m_type->getText());

    ASSERT_TRUE(Parse("2_2:Int32;"));
    ASSERT_EQ("2_2:Int32", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int32", ast->m_type->getText());

    ASSERT_TRUE(Parse("-1:Int8;"));
    ASSERT_EQ("-1:Int8", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int8", ast->m_type->getText());

    ASSERT_TRUE(Parse("-1 :Int64;"));
    ASSERT_EQ("-1:Int64", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int64", ast->m_type->getText());

    ASSERT_TRUE(Parse("256 :Int16;"));
    ASSERT_EQ("256:Int16", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int16", ast->m_type->getText());

    ASSERT_TRUE(Parse("10_000    :Int64;"));
    ASSERT_EQ("10_000:Int64", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int64", ast->m_type->getText());

    ASSERT_TRUE(Parse("100_000:  Int32;"));
    ASSERT_EQ("100_000:Int32", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int32", ast->m_type->getText());

    ASSERT_TRUE(Parse("-100_000:  Int32;"));
    ASSERT_EQ("-100_000:Int32", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Int32", ast->m_type->getText());

    ASSERT_TRUE(Parse("1.0  :  Float32;"));
    ASSERT_EQ("1.0:Float32", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Float32", ast->m_type->getText());

    ASSERT_TRUE(Parse("-0.0   :Float64;"));
    ASSERT_EQ("-0.0:Float64", ast->toString());
    ASSERT_TRUE(ast->m_type);
    ASSERT_EQ(":Float64", ast->m_type->getText());
}

TEST_F(ParserTest, TensorType) {
    ASSERT_TRUE(Parse("term:Int8[1,2] := [ [1,2,],[3,4,],];"));
    ASSERT_EQ("term:Int8[1,2] := [[1, 2,], [3, 4,],];", ast->toString());

    ASSERT_TRUE(Parse("term[..., 3] := 0;"));
    ASSERT_EQ("term[..., 3] := 0;", ast->toString());

    ASSERT_TRUE(Parse("term []= [2, 3,]:Int32;"));
    ASSERT_EQ("term []= [2, 3,]:Int32;", ast->toString());

    ASSERT_TRUE(Parse(":Bool();"));
    ASSERT_TRUE(Parse(":Bool[...]();"));
    ASSERT_TRUE(Parse(":Bool[1]();"));

    ASSERT_TRUE(Parse(":Bool[_]();"));
    ASSERT_TRUE(Parse("1.._..2"));
    ASSERT_TRUE(Parse("1.._"));
    ASSERT_TRUE(Parse(":Bool[1.._..2]();"));
    ASSERT_TRUE(Parse(":Bool[1.._]();"));

    //    ASSERT_TRUE(Parse("term[1, 3] :$type []= [[1,2,3,],];"));
    //    ASSERT_EQ("term[1, 3]:$type []= [[1, 2, 3,],];", ast->toString());

    //    ASSERT_EQ(TermID::TERM, ast->getTermID()) << EnumStr(ast->getTermID());
    //    ASSERT_EQ("term", ast->getText());
    //    ASSERT_TRUE(ast->m_right);
    //
    //    ASSERT_EQ(TermID::INDEX, ast->m_right->getTermID()) << EnumStr(ast->getTermID());
    //    ASSERT_EQ(2, ast->m_right->size());
    //    ASSERT_EQ("1", ast->m_right->at(0)->getText());
    //    ASSERT_EQ("2", ast->m_right->at(1)->getText());
    //
    //    ASSERT_TRUE(Parse("term[1, 1..2, 3];"));
    //
    //    ASSERT_EQ(TermID::INDEX, ast->m_right->getTermID()) << EnumStr(ast->getTermID());
    //    ASSERT_EQ(3, ast->m_right->size());
    //    ASSERT_EQ("1", ast->m_right->at(0)->getText());
    //    ASSERT_EQ("1..2", ast->m_right->at(1)->toString());
    //    ASSERT_EQ("3", ast->m_right->at(2)->getText());
}

TEST_F(ParserTest, DictType) {
    ASSERT_TRUE(Parse("(1,2,)"));
    ASSERT_EQ("(1, 2,)", ast->toString());

    ASSERT_TRUE(Parse("(1, arg=2,)"));
    ASSERT_EQ("(1, arg=2,)", ast->toString());

    ASSERT_TRUE(Parse("(1, arg=2, '',)"));
    ASSERT_EQ("(1, arg=2, '',)", ast->toString());

    ASSERT_TRUE(Parse("(1, .arg=2,)"));
    ASSERT_EQ("(1, arg=2,)", ast->toString());

    ASSERT_TRUE(Parse("(1, arg=2, '',)"));
    ASSERT_EQ("(1, arg=2, '',)", ast->toString());
    //    ASSERT_TRUE(Parse("(arg1=, arg2=22, arg3=,):Enum"));
    //    ASSERT_EQ("(arg1=, arg2=22, arg3=,):Enum", ast->toString());

    ASSERT_TRUE(Parse("(1, arg=2, '',):Class"));
    ASSERT_EQ("(1, arg=2, '',):Class", ast->toString());
}

TEST_F(ParserTest, TermNoArg) {
    ASSERT_TRUE(Parse("term();"));
    ASSERT_TRUE(ast->isCall());
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());

    ASSERT_TRUE(Parse("term();"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
}

TEST_F(ParserTest, TermFullName2) {
    ASSERT_TRUE(Parse("term::name::name2;"));
    ASSERT_EQ("term::name::name2", ast->toString());
}

TEST_F(ParserTest, TermFullName3) {
    ASSERT_TRUE(Parse("term::name::name2();"));
    ASSERT_EQ("term::name::name2()", ast->toString());
}

TEST_F(ParserTest, TermFullName4) {
    ASSERT_TRUE(Parse("term::name::name2();"));
    ASSERT_EQ("term::name::name2()", ast->toString());
}

TEST_F(ParserTest, TermFullName5) {
    ASSERT_TRUE(Parse("::term::name::name2;"));
    ASSERT_EQ("::term::name::name2", ast->toString());
}

TEST_F(ParserTest, TermFullName6) {
    ASSERT_TRUE(Parse("::term::name::name2();"));
    ASSERT_EQ("::term::name::name2()", ast->toString());
}

TEST_F(ParserTest, TermFullName7) {
    ASSERT_TRUE(Parse("::term::name::name2();"));
    ASSERT_EQ("::term::name::name2()", ast->toString());
}

TEST_F(ParserTest, TermFullName8) {
    ASSERT_TRUE(Parse("::name2(arg);"));
    ASSERT_EQ("::name2(arg)", ast->toString());
}

TEST_F(ParserTest, TermFullName9) {
    ASSERT_TRUE(Parse("name::name2(arg);"));
    ASSERT_EQ("name::name2(arg)", ast->toString());
}

TEST_F(ParserTest, FuncNoArg) {
    ASSERT_TRUE(Parse("@term();"));
    ASSERT_EQ(TermID::MACRO, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("@term", ast->getText());
}

TEST_F(ParserTest, TermNoArgSpace) {
    ASSERT_TRUE(Parse("term(  );"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(0, ast->size());
}

TEST_F(ParserTest, TermArgTerm) {
    ASSERT_TRUE(Parse("term(arg);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("arg", ast->at(0).second->getText());

    ASSERT_TRUE(Parse("term(name=value);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());

    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    ASSERT_EQ("value", ast->at(0).second->m_right->getText());
}

TEST_F(ParserTest, TermArgTermRef) {
    ASSERT_TRUE(Parse("term(&arg);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(1, ast->size());
    // Узел-оператор & (OPERATOR_PTR) оборачивает arg.
    ASSERT_EQ(TermID::OPERATOR_PTR, ast->at(0).second->getTermID()) << trust::toString(ast->at(0).second->getTermID());
    ASSERT_EQ("&", ast->at(0).second->getText());
    ASSERT_TRUE(ast->at(0).second->m_right);
    ASSERT_EQ("arg", ast->at(0).second->m_right->getText());

    ASSERT_TRUE(Parse("term(name=&value);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());

    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    // value оборачивается узлом-оператором &.
    ASSERT_EQ(TermID::OPERATOR_PTR, ast->at(0).second->m_right->getTermID()) << trust::toString(ast->at(0).second->m_right->getTermID());
    ASSERT_TRUE(ast->at(0).second->m_right->m_right);
    ASSERT_EQ("value", ast->at(0).second->m_right->m_right->getText());

    ASSERT_TRUE(Parse("term(name=&?value);"));
    ASSERT_TRUE(Parse("term(name=&&value);"));
    ASSERT_TRUE(Parse("term(name=&*value);"));
    ASSERT_TRUE(Parse("term(&?name=value);"));
    ASSERT_TRUE(Parse("term(&&name=value);"));
    ASSERT_TRUE(Parse("term(&*name=value);"));
}

TEST_F(ParserTest, TermArgTake) {
    ASSERT_TRUE(Parse("term(*arg);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(1, ast->size());
    // take(* / *^) - оператор, остаётся в AST как узел RefTakeExpr над rval_name.
    ASSERT_EQ(TermID::TAKE, ast->at(0).second->getTermID());
    ASSERT_EQ("*", ast->at(0).second->getText());
    ASSERT_TRUE(ast->at(0).second->m_right);
    ASSERT_EQ("arg", ast->at(0).second->m_right->getText());

    ASSERT_TRUE(Parse("term(*^arg^);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(1, ast->size());
    // Имя arg^ сохраняет '^' (иммутабельность в имени), take - оператор *^.
    ASSERT_EQ(TermID::TAKE, ast->at(0).second->getTermID());
    ASSERT_EQ("*^", ast->at(0).second->getText());
    ASSERT_TRUE(ast->at(0).second->m_right);
    ASSERT_EQ("arg^", ast->at(0).second->m_right->getText());

    ASSERT_TRUE(Parse("term(name=&value);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());

    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    // value оборачивается узлом-оператором & (OPERATOR_PTR).
    ASSERT_EQ(TermID::OPERATOR_PTR, ast->at(0).second->m_right->getTermID()) << trust::toString(ast->at(0).second->m_right->getTermID());
    ASSERT_TRUE(ast->at(0).second->m_right->m_right);
    ASSERT_EQ("value", ast->at(0).second->m_right->m_right->getText());

    ASSERT_TRUE(Parse("term(name=*^value);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());

    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("name", ast->at(0).first);
    // take - оператор *^; оборачивает значение value (имя не модифицируется).
    ASSERT_EQ(TermID::TAKE, ast->at(0).second->m_right->getTermID());
    ASSERT_EQ("*^", ast->at(0).second->m_right->getText());
    ASSERT_TRUE(ast->at(0).second->m_right->m_right);
    ASSERT_EQ("value", ast->at(0).second->m_right->m_right->getText());
}

TEST_F(ParserTest, FieldTake) {
    ASSERT_TRUE(Parse("obj.field"));
    ASSERT_TRUE(Parse("obj.*field"));
    ASSERT_TRUE(Parse("obj.*^field"));
    ASSERT_TRUE(Parse("obj.*^field^"));

    ASSERT_TRUE(Parse("obj.field = 0"));
    ASSERT_TRUE(Parse("obj.*field = 0"));
    ASSERT_TRUE(Parse("obj.*^field = 0"));
    ASSERT_TRUE(Parse("obj.*^field^ = 0"));

    ASSERT_TRUE(Parse(".field = 0"));
    ASSERT_TRUE(Parse(".*field = 0"));
    ASSERT_TRUE(Parse(".*^field = 0"));
    ASSERT_TRUE(Parse(".*^field^ = 0"));
}

TEST_F(ParserTest, Misc) {

    ASSERT_NO_THROW(Parse("func(arg:Int32) := { };"));
    ASSERT_NO_THROW(Parse("func(arg:~Int32) := { };"));
    ASSERT_NO_THROW(Parse("func(arg:~~Int32) := { };"));
    ASSERT_NO_THROW(Parse("func(arg:~~~Int32) := { };"));

    ASSERT_NO_THROW(Parse("func( & arg:Int32 ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg:Int32 ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg:Int32 ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg:Int32 ) := { };"));

    // Грамматически ссылка на значение по умолчанию допустима (в т.ч. на литерал);
    // семантическая проверка «нельзя ссылку на литерал» выполняется в анализаторе.
    ASSERT_NO_THROW(Parse("func( & arg:Int32 = 0 ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg:Int32 = 0 ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg:Int32 = 0 ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg:Int32 = 0 ) := { };"));

    ASSERT_NO_THROW(Parse("func( & arg:Int32 = term ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg:Int32 = term ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg:Int32 = term ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg:Int32 = term ) := { };"));

    ASSERT_NO_THROW(Parse("func( & arg ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg ) := { };"));

    ASSERT_NO_THROW(Parse("func( & arg = func() ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg = func() ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg = func() ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg = func() ) := { };"));

    ASSERT_NO_THROW(Parse("func( & arg = 0 ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg = 0 ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg = 0 ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg = 0 ) := { };"));

    ASSERT_NO_THROW(Parse("func( & arg = term(0) ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg = term(0) ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg = term(0) ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg = term(0) ) := { };"));

    ASSERT_NO_THROW(Parse("func( & arg:Int32 = 0 ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg:~Int32 = 0 ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg:~~Int32 = 0 ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg:~~~Int32 = 0 ) := { };"));

    ASSERT_NO_THROW(Parse("func( & arg:Int32 = val ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg:~Int32 = val ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg:~~Int32 = val ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg:~~~Int32 = val ) := { };"));

    ASSERT_NO_THROW(Parse("func( & arg:Int32 = term(0) ) := { };"));
    ASSERT_NO_THROW(Parse("func( &? arg:~Int32 = term(0) ) := { };"));
    ASSERT_NO_THROW(Parse("func( &* arg:~~Int32 = term(0) ) := { };"));
    ASSERT_NO_THROW(Parse("func( && arg:~~~Int32 = term(0) ) := { };"));

    ASSERT_NO_THROW(Parse("func( arg = & val );"));
    ASSERT_NO_THROW(Parse("func( arg = &? val  );"));
    ASSERT_NO_THROW(Parse("func( arg = &* val );"));
    ASSERT_NO_THROW(Parse("func( arg = && val );"));

    ASSERT_NO_THROW(Parse(":RefInt32 := & :Int32;"));
    ASSERT_NO_THROW(Parse(":RefInt32 := &? :Int32;"));
    ASSERT_NO_THROW(Parse(":RefInt32 := &* :Int32;"));
    ASSERT_NO_THROW(Parse(":RefInt32 := && :Int32;"));

    ASSERT_NO_THROW(Parse("var:Int32 := 0;"));
    ASSERT_NO_THROW(Parse("& var:Int32 := 0;"));
    ASSERT_NO_THROW(Parse("&? var:Int32 := 0;"));
    ASSERT_NO_THROW(Parse("&& var:Int32 := 0;"));
    ASSERT_NO_THROW(Parse("&* var:Int32 := 0;"));

    ASSERT_NO_THROW(Parse("var:Int32 := 0;"));
    ASSERT_NO_THROW(Parse("&(var, __timeout__=1000) var:Int32 := 0;"));
    ASSERT_NO_THROW(Parse("&?(var, __timeout__=1000) var:Int32 := 0;"));
    ASSERT_NO_THROW(Parse("&&(var, __timeout__=1000) var:Int32 := 0;"));
    ASSERT_NO_THROW(Parse("&*(var, __timeout__=1000) var:Int32 := 0;"));
}

TEST_F(ParserTest, TermArgTermSpace) {
    ASSERT_TRUE(Parse("   \n  \t term(  \n  arg  \n  )    ;   \n  "));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("arg", ast->at(0).second->getText());
}

TEST_F(ParserTest, TermArgs1) {
    ASSERT_TRUE(Parse("term(arg1);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("arg1", ast->at(0).second->getText());
}

TEST_F(ParserTest, TermArgs2) {
    ASSERT_TRUE(Parse("term(arg1,arg2);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("arg1", ast->at(0).second->getText());
    ASSERT_EQ("arg2", ast->at(1).second->getText());
}

TEST_F(ParserTest, TermArgsRef) {
    ASSERT_TRUE(Parse("term(*arg1, *^arg2);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(2, ast->size());

    ASSERT_TRUE(Parse("term(. name1=*arg1, . name2=*^arg2);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());

    ASSERT_TRUE(Parse("term(name1=*arg1, name2=*^arg2);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());

    ASSERT_TRUE(Parse("term(*(arg1), *^(arg2));"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());

    ASSERT_TRUE(Parse("term(name1=*(arg1), name2=*^(arg2));"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(2, ast->size());

    ASSERT_TRUE(Parse("term(&arg1, &&arg2, &*arg3);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(3, ast->size());

    ASSERT_TRUE(Parse("term(name1=&arg1, name2=&&arg2, name3=&*arg3);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(3, ast->size());

    ASSERT_TRUE(Parse("term(&^arg1, &&^arg2, &*^arg3);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(3, ast->size());

    ASSERT_TRUE(Parse("term(name1=&^arg1, name2=&&^arg2, name3=&*^arg3);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(3, ast->size());
}

TEST_F(ParserTest, TermArgMixed) {
    ASSERT_TRUE(Parse("term(\narg1,\n arg2\n = \narg3 \n);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());

    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("arg1", ast->at(0).second->getText());
    ASSERT_EQ("arg2", ast->at(1).first);
    ASSERT_EQ("arg3", ast->at(1).second->m_right->getText());
}

TEST_F(ParserTest, ArgsType) {
    ASSERT_TRUE(Parse("term(bool:Bool=term(100), int:Int32=100, long:Int64=@term()):Float64:={long;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term(bool:Bool=term(100), int:Int32=100, long:Int64=@term()):Float64 := {long;};", ast->toString());

    ASSERT_TRUE(Parse("term(&bool:~Bool=term(100), &* int:~Int32=name::name, &? long:~~Int64=@term()):~~~Float64:={long;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    //    ASSERT_EQ("term(bool:Bool=&term(100), int:Int32=&*name::name, long:Int64=&?@term()):Float64 := {long;};;", ast->toString());
}

TEST_F(ParserTest, ArgsType1) {
    ASSERT_TRUE(Parse("term(long:Int64=@term());"));
    ASSERT_EQ("term(long:Int64=@term())", ast->toString());
}

TEST_F(ParserTest, Any) {
    ASSERT_TRUE(Parse("term((,));"));
    ASSERT_TRUE(Parse("term([1,]);"));
    ASSERT_TRUE(Parse("term((,):Class);"));
    ASSERT_TRUE(Parse("term([1,]:Int8);"));
    ASSERT_TRUE(Parse("term(1..2);"));
    ASSERT_TRUE(Parse("term(1..2..1\\1);"));

    ASSERT_TRUE(Parse("term(name=(,));"));
    ASSERT_TRUE(Parse("term(name=[1,]);"));
    ASSERT_TRUE(Parse("term(name=(,):Class);"));
    ASSERT_TRUE(Parse("term(name=[1,]:Int8);"));
    ASSERT_TRUE(Parse("term(name=1..2);"));
    ASSERT_TRUE(Parse("term(name=1..2..1\\1);"));

    ASSERT_TRUE(Parse("term(1, name=(,));"));
    ASSERT_TRUE(Parse("term(1, name=[1,]);"));
    ASSERT_TRUE(Parse("term(1, name=(,):Class);"));
    ASSERT_TRUE(Parse("term(1, name=[1,]:Int8);"));
    ASSERT_TRUE(Parse("term(1, name=1..2);"));
    ASSERT_TRUE(Parse("term(1, name=1..2..1\\1);"));
}

TEST_F(ParserTest, TermCall) {
    ASSERT_TRUE(Parse("var2 := min(200, var, 400);"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(":=", ast->getText());

    TermPtr right = ast->m_right;
    ASSERT_TRUE(right);
    ASSERT_EQ(3, right->size());
    ASSERT_EQ("200", right->at(0).second->getText());
    ASSERT_FALSE(right->at(0).second->m_left);
    ASSERT_FALSE(right->at(0).second->m_right);
    ASSERT_EQ("var", right->at(1).second->getText());
    ASSERT_FALSE(right->at(1).second->m_left);
    ASSERT_FALSE(right->at(1).second->m_right);
    ASSERT_EQ("400", right->at(2).second->getText());
    ASSERT_FALSE(right->at(2).second->m_left);
    ASSERT_FALSE(right->at(2).second->m_right);
}

TEST_F(ParserTest, TermCollection) {
    ASSERT_TRUE(Parse("term([2,], [arg1,arg2,]);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ(TermID::TENSOR, ast->at(0).second->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(TermID::TENSOR, ast->at(1).second->getTermID()) << trust::toString(ast->getTermID());
}

TEST_F(ParserTest, TermCollection2) {
    ASSERT_TRUE(Parse("term((,), name=[arg1,arg2,]);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ(TermID::DICT, ast->at(0).second->getTermID()) << trust::toString(ast->at(0).second->getTermID());
    // Именованный аргумент - ARGUMENT-обёртка, значение (TENSOR) в m_right
    ASSERT_EQ(TermID::ARGUMENT, ast->at(1).second->getTermID()) << trust::toString(ast->at(1).second->getTermID());
    ASSERT_TRUE(ast->at(1).second->m_right);
    ASSERT_EQ(TermID::TENSOR, ast->at(1).second->m_right->getTermID()) << trust::toString(ast->at(1).second->m_right->getTermID());
}

TEST_F(ParserTest, OpsCall) {
    ASSERT_TRUE(Parse("call(1+1)"));
    ASSERT_TRUE(Parse("call(1-1)"));
    ASSERT_TRUE(Parse("call(1\\1-1)"));
    ASSERT_TRUE(Parse("call(term+1)"));
    ASSERT_TRUE(Parse("call(term-1)"));
    ASSERT_TRUE(Parse("call(term+term)"));
    ASSERT_TRUE(Parse("call(term-term)"));
    ASSERT_TRUE(Parse("call(term-term,term*2)"));
}

TEST_F(ParserTest, ArgMixedFail) {
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("term(arg1,arg2=arg3,,);"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("term(,);"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(ParserTest, MathPlus) {
    ASSERT_TRUE(Parse("test         :=       123+456;"));
    ASSERT_EQ(":=", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("test", ast->m_left->getText());
    TermPtr op = ast->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("+", op->getText());
    ASSERT_TRUE(op->m_right);
    ASSERT_TRUE(op->m_left);
    ASSERT_EQ("123", op->m_left->getText());
    ASSERT_EQ("456", op->m_right->getText());
}

TEST_F(ParserTest, MathMinus) {
    ASSERT_TRUE(Parse("test := 123-456;"));
    ASSERT_EQ(":=", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("test", ast->m_left->getText());
    TermPtr op = ast->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("-", op->getText());
    ASSERT_TRUE(op->m_right);
    ASSERT_TRUE(op->m_left);
    ASSERT_EQ("123", op->m_left->getText());
    ASSERT_EQ("456", op->m_right->getText());
}

TEST_F(ParserTest, MathMul) {
    ASSERT_TRUE(Parse("test := 123*456;"));
    ASSERT_EQ(":=", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("test", ast->m_left->getText());
    TermPtr op = ast->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("*", op->getText());
    ASSERT_TRUE(op->m_right);
    ASSERT_TRUE(op->m_left);
    ASSERT_EQ("123", op->m_left->getText());
    ASSERT_EQ("456", op->m_right->getText());
}

TEST_F(ParserTest, MathDiv) {
    ASSERT_TRUE(Parse("test := 123/456;"));
    ASSERT_EQ(":=", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("test", ast->m_left->getText());
    TermPtr op = ast->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("/", op->getText());
    ASSERT_TRUE(op->m_right);
    ASSERT_TRUE(op->m_left);
    ASSERT_EQ("123", op->m_left->getText());
    ASSERT_EQ("456", op->m_right->getText());
}

TEST_F(ParserTest, MathNeg) {
    ASSERT_TRUE(Parse("test := -456;"));
    ASSERT_EQ(":=", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("test", ast->m_left->getText());
    TermPtr op = ast->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("-456", op->getText());

    ASSERT_TRUE(Parse("1000-456"));
    ASSERT_EQ("-", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("1000", ast->m_left->getText());
    op = ast->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("456", op->getText());

    ASSERT_TRUE(Parse("-(456)"));
    ASSERT_EQ("-", ast->getText());
    ASSERT_FALSE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    op = ast->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("456", op->getText());

    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("1000 456"));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(ParserTest, MathPrioritet) {
    ASSERT_TRUE(Parse("1+2*3"));
    ASSERT_EQ("+", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("1", ast->m_left->getText());
    TermPtr op = ast->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("*", op->getText());
    ASSERT_TRUE(op->m_right);
    ASSERT_TRUE(op->m_left);
    ASSERT_EQ("2", op->m_left->getText());
    ASSERT_EQ("3", op->m_right->getText());

    ASSERT_TRUE(Parse("1*2+3"));
    ASSERT_EQ("+", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("3", ast->m_right->getText());
    op = ast->m_left;
    ASSERT_TRUE(op);
    ASSERT_EQ("*", op->getText());
    ASSERT_TRUE(op->m_right);
    ASSERT_TRUE(op->m_left);
    ASSERT_EQ("1", op->m_left->getText());
    ASSERT_EQ("2", op->m_right->getText());

    ASSERT_TRUE(Parse("(1*2)+3"));
    ASSERT_EQ("+", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("3", ast->m_right->getText());
    op = ast->m_left;
    ASSERT_TRUE(op);
    ASSERT_EQ("*", op->getText());
    ASSERT_TRUE(op->m_right);
    ASSERT_TRUE(op->m_left);
    ASSERT_EQ("1", op->m_left->getText());
    ASSERT_EQ("2", op->m_right->getText());

    ASSERT_TRUE(Parse("1*(2+3)"));
    ASSERT_EQ("*", ast->getText());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("1", ast->m_left->getText());
    op = ast->m_right;
    ASSERT_TRUE(op);
    ASSERT_EQ("+", op->getText());
    ASSERT_TRUE(op->m_right);
    ASSERT_TRUE(op->m_left);
    ASSERT_EQ("2", op->m_left->getText());
    ASSERT_EQ("3", op->m_right->getText());
}

TEST_F(ParserTest, CodeSimple) {
    ASSERT_TRUE(Parse("{%code+code%};"));
    ASSERT_EQ("code+code", ast->getText());
}

TEST_F(ParserTest, CodeSimple2) {
    ASSERT_TRUE(Parse("{% code+code %};"));
    ASSERT_EQ(" code+code ", ast->getText());
}

TEST_F(ParserTest, Brakets1) {
    ASSERT_TRUE(Parse("(1+2)"));
    ASSERT_EQ("1 + 2", ast->toString());
}

TEST_F(ParserTest, Brakets2) {
    ASSERT_TRUE(Parse("(1==2)"));
    ASSERT_EQ("1 == 2", ast->toString());
}

TEST_F(ParserTest, Brakets3) {
    ASSERT_TRUE(Parse("(call())"));
    ASSERT_EQ("call()", ast->toString());
}

TEST_F(ParserTest, Brakets4) {
    ASSERT_TRUE(Parse("(:call())"));
    ASSERT_EQ(":call()", ast->toString());
}

TEST_F(ParserTest, Brakets5) {
    ASSERT_TRUE(Parse("(:call()==0)"));
    ASSERT_EQ(":call() == 0", ast->toString());
}

TEST_F(ParserTest, AssignSimple) {
    ASSERT_TRUE(Parse("term := term2;"));
    ASSERT_EQ("term := term2;", ast->toString());
}

TEST_F(ParserTest, AssignSimple2) {
    ASSERT_TRUE(Parse("\t term   :=   term2()  ;  \n"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("term", ast->m_left->getText());
    ASSERT_EQ(0, ast->m_left->size());
    ASSERT_EQ("term2", ast->m_right->getText());
    ASSERT_EQ(0, ast->m_right->size());
    ASSERT_EQ("term := term2();", ast->toString());
}

TEST_F(ParserTest, AssignFullName) {
    ASSERT_TRUE(Parse("term::name() := {term2;};"));
    ASSERT_EQ("term::name() := {term2;};", ast->toString());
}

TEST_F(ParserTest, AssignClass0) {
    ASSERT_TRUE(Parse("term := :Class();"));
    ASSERT_EQ("term := :Class();", ast->toString());
}

TEST_F(ParserTest, AssignClass1) {
    ASSERT_TRUE(Parse(":class  :=    :Class() {}  ;"));
    ASSERT_EQ(":class := :Class(){};", ast->toString());
}

TEST_F(ParserTest, AssignClass2) {
    ASSERT_TRUE(Parse(":class  :=  ::ns::func(arg1, arg2=\"\") {};"));
    ASSERT_EQ(":class := ::ns::func(arg1, arg2=\"\"){};", ast->toString());
}

TEST_F(ParserTest, Namespace) {
    ASSERT_TRUE(Parse("name{ func() := {}  };"));
    ASSERT_TRUE(Parse("name::space{ func() := {}  };"));
    ASSERT_TRUE(Parse("::name::space{ func() := {}  };"));
    ASSERT_TRUE(Parse("::{ func() := {}  };"));
}

// Метка блока (`ns { }`) должна попадать в ScopeBlock.name нового AST.
// (критерий приёмки задачи: ScopeBlock.name не пуст для `ns { }`)
TEST_F(ParserTest, NamespaceToScopeBlockName) {
    ASSERT_TRUE(Parse("ns { x := 1; };"));

    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast->isBlock());
    ASSERT_EQ("ns", ast->getText());

    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* sb = dynamic_cast<ScopeBlock*>(nodes[0].get());
    ASSERT_TRUE(sb);
    ASSERT_EQ("ns", sb->name());
    ASSERT_FALSE(sb->is_anonymous());
}

// Иммутабельность '^' не применима к меткам блоков/областям имён:
// termToAst должен выдать ошибку и НЕ проставить attr::ReadOnly.
TEST_F(ParserTest, NamespaceImmutableError) {
    ASSERT_TRUE(Parse("ns^ { x := 1; };"));

    m_ctx.diag().clear();
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_GT(m_ctx.diag().errorCount(), 0) << "'^' in block label must produce an error";

    ASSERT_EQ(1, nodes.size());
    auto* sb = dynamic_cast<ScopeBlock*>(nodes[0].get());
    ASSERT_TRUE(sb);
    ASSERT_EQ("ns", sb->name());
    ASSERT_FALSE(sb->has_attr(m_ctx.attrs(), attr::ReadOnly));
}

// take(*^) при конвертации в AstNode: крышечка срезается (текст "*"),
// признак иммутабельности уходит в attr::ReadOnly.
TEST_F(ParserTest, TakeConstToAst) {
    ASSERT_TRUE(Parse("term(*^arg^);"));

    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());

    // term(*^arg^) - вызов: корневой узел CallExpr (callee = имя term, args = [операнд]).
    auto* termNode = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(termNode);
    ASSERT_EQ("term", termNode->m_callee->text());
    ASSERT_TRUE(termNode->m_args && !termNode->m_args->empty());

    auto* take = dynamic_cast<Sequence*>(termNode->m_args->at(0).get());
    ASSERT_TRUE(take);
    ASSERT_EQ(ParserToken::Kind::RefTakeExpr, take->kind());
    ASSERT_EQ("*", take->text()) << "'^' must be stripped from take text";
    ASSERT_TRUE(take->has_attr(m_ctx.attrs(), attr::ReadOnly));

    // arg^ - имя с иммутабельностью: срезается '^', проставляется attr::ReadOnly.
    auto* arg = dynamic_cast<AstNodeAttr*>(take->m_body[0].get());
    ASSERT_TRUE(arg);
    ASSERT_EQ("arg", arg->text());
    ASSERT_TRUE(arg->has_attr(m_ctx.attrs(), attr::ReadOnly));
}

// ptr(&) при конвертации в AstNode: узел-оператор становится RefMakeExpr,
// операнд - его телом; иммутабельность &^ → attr::ReadOnly.
TEST_F(ParserTest, RefMakeToAst) {
    ASSERT_TRUE(Parse("term(&arg);"));

    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());

    auto* termNode = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(termNode);
    ASSERT_EQ("term", termNode->m_callee->text());
    ASSERT_TRUE(termNode->m_args && !termNode->m_args->empty());

    auto* refMake = dynamic_cast<Sequence*>(termNode->m_args->at(0).get());
    ASSERT_TRUE(refMake);
    ASSERT_EQ(ParserToken::Kind::RefMakeExpr, refMake->kind());
    ASSERT_EQ("&", refMake->text());
    ASSERT_FALSE(refMake->has_attr(m_ctx.attrs(), attr::ReadOnly));

    auto* arg = dynamic_cast<AstNodeAttr*>(refMake->m_body[0].get());
    ASSERT_TRUE(arg);
    ASSERT_EQ("arg", arg->text());
    ASSERT_FALSE(arg->has_attr(m_ctx.attrs(), attr::ReadOnly));

    // &^ - иммутабельность оператора → attr::ReadOnly на RefMakeExpr.
    ASSERT_TRUE(Parse("term(&^arg);"));
    nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    termNode = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(termNode);
    refMake = dynamic_cast<Sequence*>(termNode->m_args->at(0).get());
    ASSERT_TRUE(refMake);
    ASSERT_EQ(ParserToken::Kind::RefMakeExpr, refMake->kind());
    ASSERT_EQ("&", refMake->text());
    ASSERT_TRUE(refMake->has_attr(m_ctx.attrs(), attr::ReadOnly));

    // &arg^ - иммутабельность операнда → attr::ReadOnly на arg, не на операторе.
    ASSERT_TRUE(Parse("term(&arg^);"));
    nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    termNode = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(termNode);
    refMake = dynamic_cast<Sequence*>(termNode->m_args->at(0).get());
    ASSERT_TRUE(refMake);
    ASSERT_EQ(ParserToken::Kind::RefMakeExpr, refMake->kind());
    ASSERT_EQ("&", refMake->text());
    ASSERT_FALSE(refMake->has_attr(m_ctx.attrs(), attr::ReadOnly));
    arg = dynamic_cast<AstNodeAttr*>(refMake->m_body[0].get());
    ASSERT_TRUE(arg);
    ASSERT_EQ("arg", arg->text());
    ASSERT_TRUE(arg->has_attr(m_ctx.attrs(), attr::ReadOnly));
}
// ELLIPSIS ("...") - синтаксис, распознанный лексером/грамматикой в аргументах - теперь
// конвертируется в AST-узел (kind=Ellipsis, Sequence). Ранее (без Kind) конвертация FAULT.
TEST_F(ParserTest, EllipsisToAst) {
    m_ctx.diag().clear();
    auto term = Term::Create(TermID::ELLIPSIS, "...");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(term, m_ctx);
    ASSERT_EQ(0, m_ctx.diag().errorCount()) << "ELLIPSIS must convert without error (not FAULT)";
    ASSERT_EQ(1, nodes.size());
    auto* ell = dynamic_cast<Sequence*>(nodes[0].get());
    ASSERT_TRUE(ell);
    ASSERT_EQ(ParserToken::Kind::Ellipsis, ell->kind());
    ASSERT_EQ("...", ell->text());
}

// TypeName-терм-конструктор - единственный владелец раскладки TYPE-терма:
//   m_dims   из term->m_type (ARGS-терм размерностей `[...]`)
//   m_params из term->m_args (call-аргументы `(...)`)
TEST_F(ParserTest, TypeNameDimsParamsToAst) {
    ASSERT_TRUE(Parse("m:Matrix[2,3](Float) := mat;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    auto* t = dynamic_cast<IdentType*>(vd->m_type.get());
    ASSERT_TRUE(t);
    ASSERT_EQ("Matrix", t->text());
    ASSERT_TRUE(t->dims() && t->dims()->size() == 2) << "dims из [...]: 2 измерения";
    ASSERT_TRUE(t->params() && t->params()->size() == 1) << "params из (...): 1 generic-параметр";
}

TEST_F(ParserTest, TypeNameParamsOnlyToAst) {
    // Параметризованная аннотация с ТИПИЗИРОВАННЫМИ аргументами `Pair(:Int, :String)` → IdentType
    // с параметрами. (Голые имена `Pair(Int, String)` - это value-форма/вызов → DictLiteralNode,
    // см. TypeCastExprToAst; аннотация с параметрами пишется с типами-аргументами.)
    ASSERT_TRUE(Parse("p:Pair(:Int, :String) := 0;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    auto* t = dynamic_cast<IdentType*>(vd->m_type.get());
    ASSERT_TRUE(t);
    ASSERT_EQ("Pair", t->text());
    ASSERT_FALSE(t->dims()) << "нет dims";
    ASSERT_TRUE(t->params() && t->params()->size() == 2) << "params из (...): 2 аргумента";
}

TEST_F(ParserTest, TypeCastExprToAst) {
    // `:Type(expr)` в позиции выражения → ЕДИНЫЙ узел DictLiteralNode с аннотацией типа
    // m_type=TypeName и элементами (имя=значение). Класс узла (кортеж/каст/конструктор)
    // определяет анализатор по типу из реестра (kind CastExpr упразднён).
    ASSERT_TRUE(Parse("b:Int8 := :Int8(a);"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_TRUE(vd->m_initializer);
    auto* dl = dynamic_cast<DictLiteralNode*>(vd->m_initializer.get());
    ASSERT_TRUE(dl);
    EXPECT_EQ(dl->kind(), ParserToken::Kind::DictLiteral);
    ASSERT_TRUE(dl->m_type);
    EXPECT_EQ(dl->m_type->kind(), ParserToken::Kind::TypeName);
    EXPECT_EQ(dl->m_type->text(), "Int8");
    ASSERT_EQ(dl->m_body.size(), 1);
    ASSERT_TRUE(dl->m_body[0]);
    EXPECT_EQ(dl->m_body[0]->kind(), ParserToken::Kind::ArgNode);
    const auto& b = static_cast<const ArgNode&>(*dl->m_body[0]);
    EXPECT_TRUE(b.text().empty()) << "безымянный элемент";
    ASSERT_TRUE(b.m_value);
    EXPECT_EQ(b.m_value->kind(), ParserToken::Kind::Ident);
    EXPECT_EQ(b.m_value->text(), "a");
}

// -- Диапазон `start..stop[..step]` (TermID::RANGE) → RangeExpr --
// Терм `range` хранит операнды в m_args с именами start/stop/step; конвертер строит
// m_body=[start, stop, (step)] и переносит аннотации типа операндов в operandTypes.
TEST_F(ParserTest, RangeExprToAst) {
    auto r = Term::Create(TermID::RANGE, "..");
    r->push_back(Term::Create(TermID::INTEGER, "1"), "start");
    r->push_back(Term::Create(TermID::INTEGER, "10"), "stop");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(r, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* re = dynamic_cast<RangeExpr*>(nodes[0].get());
    ASSERT_TRUE(re);
    EXPECT_EQ(re->kind(), ParserToken::Kind::RangeExpr);
    ASSERT_EQ(re->m_body.size(), 2);
    EXPECT_EQ(re->start()->kind(), ParserToken::Kind::IntLiteral);
    EXPECT_EQ(re->stop()->kind(), ParserToken::Kind::IntLiteral);
    EXPECT_FALSE(re->hasStep());
}

TEST_F(ParserTest, RangeExprWithStepToAst) {
    auto r = Term::Create(TermID::RANGE, "..");
    r->push_back(Term::Create(TermID::INTEGER, "0"), "start");
    r->push_back(Term::Create(TermID::INTEGER, "10"), "stop");
    r->push_back(Term::Create(TermID::NUMBER, "0.01"), "step");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(r, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* re = dynamic_cast<RangeExpr*>(nodes[0].get());
    ASSERT_TRUE(re);
    ASSERT_EQ(re->m_body.size(), 3);
    EXPECT_TRUE(re->hasStep());
    EXPECT_EQ(re->step()->kind(), ParserToken::Kind::FloatLiteral);
}

TEST_F(ParserTest, RangeExprTypedStopToAst) {
    // `0..100:Rational` - stop-операнд с аннотацией типа (грамматика `digits_literal type_item`
    // кладёт её в m_type) → конвертер переносит её в RangeExpr::operandTypes[1] (TypeName Rational).
    auto r = Term::Create(TermID::RANGE, "..");
    r->push_back(Term::Create(TermID::INTEGER, "0"), "start");
    auto stop = Term::Create(TermID::INTEGER, "100");
    stop->m_type = Term::Create(TermID::TYPE, ":Rational");
    r->push_back(stop, "stop");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(r, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* re = dynamic_cast<RangeExpr*>(nodes[0].get());
    ASSERT_TRUE(re);
    ASSERT_EQ(re->m_body.size(), 2);
    ASSERT_EQ(re->operandTypes.size(), 2);
    EXPECT_EQ(re->operandTypes[0], nullptr);
    ASSERT_TRUE(re->operandTypes[1]);
    EXPECT_EQ(re->operandTypes[1]->kind(), ParserToken::Kind::TypeName);
    EXPECT_EQ(re->operandTypes[1]->text(), "Rational");
}

TEST_F(ParserTest, TypeNameDimsOnlyToAst) {
    ASSERT_TRUE(Parse("l:List[Int] := 0;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    auto* t = dynamic_cast<IdentType*>(vd->m_type.get());
    ASSERT_TRUE(t);
    ASSERT_EQ("List", t->text());
    ASSERT_TRUE(t->dims() && t->dims()->size() == 1) << "dims из [...]: 1 размерность";
    ASSERT_FALSE(t->params()) << "нет params";
}

// -- Класс-селекция Ident→CallExpr vs IdentName --
// Голое имя (без детей) → IdentName; вызов (есть дети) → CallExpr.
TEST_F(ParserTest, IdentBareToIdentName) {
    auto t = Term::Create(TermID::NAME, "x");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(t, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* id = dynamic_cast<IdentName*>(nodes[0].get());
    ASSERT_TRUE(id);
    ASSERT_EQ("x", id->text());
}

TEST_F(ParserTest, IdentCallToCallExpr) {
    auto t = Term::Create(TermID::NAME, "f");
    t->m_args.emplace();
    t->push_back(Term::Create(TermID::NAME, "a"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(t, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* call = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(call);
    ASSERT_TRUE(call->m_callee);
    ASSERT_EQ("f", call->m_callee->text());
    ASSERT_TRUE(call->m_args && call->m_args->size() == 1);
}

// f() (пустые args) → CallExpr: наличие m_args (даже пустого) = вызов по структурному
// предикату `m_args || m_sequence || m_left || m_right`. IdentName остаётся только для
// терма без m_args (голое имя, см. IdentBareToIdentName).
TEST_F(ParserTest, IdentEmptyCallToCallExpr) {
    auto t = Term::Create(TermID::NAME, "f");
    t->m_args.emplace(); // f()
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(t, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* call = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(call) << "f() (пустые args) → CallExpr (вызов без аргументов)";
    ASSERT_TRUE(call->m_callee);
    ASSERT_EQ("f", call->m_callee->text());
}

// Именованный аргумент (name=value) → Binary(AssignOp) внутри args (visit_ARGUMENT class-select).
TEST_F(ParserTest, CallNamedArgToArgNode) {
    auto t = Term::Create(TermID::NAME, "f");
    t->m_args.emplace();
    auto arg = Term::Create(TermID::ARGUMENT, "");
    arg->m_left = Term::Create(TermID::NAME, "x");
    arg->m_right = Term::Create(TermID::INTEGER, "5");
    t->push_back(arg, "x");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(t, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* call = dynamic_cast<CallExpr*>(nodes[0].get());
    ASSERT_TRUE(call);
    ASSERT_TRUE(call->m_args && call->m_args->size() == 1);
    auto* a = dynamic_cast<ArgNode*>(call->m_args->at(0).get());
    ASSERT_TRUE(a);
    ASSERT_EQ("x", a->text());
    ASSERT_TRUE(a->m_value);
    ASSERT_EQ("5", a->m_value->text());
}

// -- VarDecl (visit_CREATE_NAME → VarDecl-конструктор) --
TEST_F(ParserTest, VarDeclSimpleToAst) {
    ASSERT_TRUE(Parse("x := 5;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_EQ("x", vd->text());
    ASSERT_FALSE(vd->m_type);
    ASSERT_TRUE(vd->m_initializer);
    ASSERT_EQ(ParserToken::Kind::IntLiteral, vd->m_initializer->kind());
}

TEST_F(ParserTest, VarDeclTypedToAst) {
    ASSERT_TRUE(Parse("x:Int := 5;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_EQ("x", vd->text());
    ASSERT_TRUE(vd->m_type);
    ASSERT_EQ(ParserToken::Kind::TypeName, vd->m_type->kind());
}

// -- Функции через CREATE_NAME (`:=`) → FuncDecl-конструктор --
// CREATE_NAME - единый узел функции И переменной; класс-селекция по m_left->isCall().
TEST_F(ParserTest, FuncDeclViaAssignToAst) {
    ASSERT_TRUE(Parse("add(a:Int, b:Int):Int := { ++ a ++; };"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* fd = dynamic_cast<FuncDecl*>(nodes[0].get());
    ASSERT_TRUE(fd);
    ASSERT_EQ("add", fd->text());
    ASSERT_TRUE(fd->m_type);
    ASSERT_EQ(ParserToken::Kind::TypeName, fd->m_type->kind());
    ASSERT_TRUE(fd->m_params && fd->m_params->size() == 2);
    for (const auto& p : *fd->m_params) {
        auto* pd = dynamic_cast<ArgNode*>(p.get());
        ASSERT_TRUE(pd);
        ASSERT_TRUE(pd->m_type) << "параметр должен нести тип (из m_right)";
    }
    ASSERT_TRUE(fd->m_body && !fd->m_body->empty()) << "функция должна иметь тело";
}

// Native-функция - тоже обычная функция через `:=` (m_left - native-идентификатор с сигнатурой).
// NATIVE отдельно не выделяется.
TEST_F(ParserTest, NativeFuncDeclToAst) {
    ASSERT_TRUE(Parse("%add(a:Int, b:Int):Int := { ++ a ++; };"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* fd = dynamic_cast<FuncDecl*>(nodes[0].get());
    ASSERT_TRUE(fd);
    ASSERT_EQ("%add", fd->text());
    ASSERT_TRUE(fd->m_type);
    ASSERT_TRUE(fd->m_params && fd->m_params->size() == 2);
    ASSERT_TRUE(fd->m_body && !fd->m_body->empty()) << "native-функция должна иметь тело";
}

// CREATE_TYPE (`::=`) - синоним типа, а НЕ функция (даже с формой вызова в m_left).
TEST_F(ParserTest, CreateTypeIsTypeSynonymToAst) {
    ASSERT_TRUE(Parse("MyInt ::= Int;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* bin = dynamic_cast<Binary*>(nodes[0].get());
    ASSERT_TRUE(bin) << "::= - это синоним типа (Binary TypeDecl), не функция";
    ASSERT_EQ(ParserToken::Kind::TypeDecl, bin->kind());
}

// Forward-объявление переменной `x:Int32 := ...;` - чистое многоточие вместо инициализатора.
TEST_F(ParserTest, ForwardVarDeclToAst) {
    ASSERT_TRUE(Parse("x:Int32 := ...;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_EQ("x", vd->text());
    ASSERT_TRUE(vd->m_type);
    ASSERT_EQ(ParserToken::Kind::TypeName, vd->m_type->kind());
    ASSERT_EQ(nullptr, vd->m_initializer) << "forward-объявление не должно иметь инициализатора";
}

// Forward-объявление переменной без типа `y := ...;` - инициализатора нет, тип опционален.
TEST_F(ParserTest, ForwardVarDeclNoTypeToAst) {
    ASSERT_TRUE(Parse("y := ...;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* vd = dynamic_cast<VarDecl*>(nodes[0].get());
    ASSERT_TRUE(vd);
    ASSERT_EQ("y", vd->text());
    ASSERT_EQ(nullptr, vd->m_type);
    ASSERT_EQ(nullptr, vd->m_initializer) << "forward-объявление не должно иметь инициализатора";
}

// Forward-объявление функции `%add(a:Int32, b:Int32):Int32 := ...;` - чистое многоточие
// вместо тела → m_body = nullopt (forward declaration).
TEST_F(ParserTest, ForwardFuncDeclToAst) {
    ASSERT_TRUE(Parse("%add(a:Int32, b:Int32):Int32 := ...;"));
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(ast, m_ctx);
    ASSERT_EQ(1, nodes.size());
    auto* fd = dynamic_cast<FuncDecl*>(nodes[0].get());
    ASSERT_TRUE(fd);
    ASSERT_EQ("%add", fd->text());
    ASSERT_TRUE(fd->m_type);
    ASSERT_TRUE(fd->m_params && fd->m_params->size() == 2);
    for (const auto& p : *fd->m_params) {
        auto* pd = dynamic_cast<ArgNode*>(p.get());
        ASSERT_TRUE(pd);
        ASSERT_TRUE(pd->m_type) << "параметр forward-функции должен нести тип";
    }
    ASSERT_FALSE(fd->m_body.has_value()) << "forward-объявление не должно иметь тела";
}

// Нереализованная конструкция (TermID без Kind, напр. await "[*]") при конвертации должна
// дать диагностику Severity::Error с позицией, а НЕ внутренний FAULT.
TEST_F(ParserTest, UnimplementedConstructReportsError) {
    m_ctx.diag().clear();
    auto term = Term::Create(TermID::AWAIT, "[*]");
    std::vector<AstNodePtr> nodes = TermToAstConverter::termToAst(term, m_ctx);
    ASSERT_GT(m_ctx.diag().errorCount(), 0) << "unimplemented construct must report an error";
    ASSERT_EQ(0, nodes.size()) << "unimplemented node must be dropped (convert returns nullptr)";
}

TEST_F(ParserTest, AssignFullName2) {
    ASSERT_TRUE(Parse("term::name::name2() := term2;"));
    ASSERT_EQ("term::name::name2() := term2;", ast->toString());
}

TEST_F(ParserTest, AssignFullName3) {
    ASSERT_TRUE(Parse("::term::name::name3() := term2;"));
    ASSERT_EQ("::term::name::name3() := term2;", ast->toString());
}

// TEST_F(ParserTest, FiledAssign) {
//     ASSERT_TRUE(Parse("$1.val :=  123;"));
//     ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_TRUE(ast->m_left);
//     ASSERT_TRUE(ast->m_right);
//
//     ASSERT_EQ(TermID::ARGUMENT, ast->m_left->getTermID());
//     ASSERT_EQ("$1", ast->m_left->getText());
//
//     ASSERT_TRUE(ast->m_left->m_right);
//     ASSERT_EQ("val", ast->m_left->m_right->getText());
//
//     ASSERT_EQ(TermID::INTEGER, ast->m_right->getTermID());
//     ASSERT_EQ("123", ast->m_right->getText());
// }

// TEST_F(ParserTest, FiledAssign2) {
//     ASSERT_TRUE(Parse("term.field1.field2 :=  123;"));
//     ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_TRUE(ast->m_left);
//     ASSERT_TRUE(ast->m_right);
//
//     ASSERT_EQ(TermID::NAME, ast->m_left->getTermID());
//     ASSERT_EQ("term", ast->m_left->getText());
//
//     ASSERT_TRUE(ast->m_left->m_right);
//     ASSERT_EQ("field1", ast->m_left->m_right->getText());
//     ASSERT_TRUE(ast->m_left->m_right->m_right);
//     ASSERT_EQ("field2", ast->m_left->m_right->m_right->getText());
//     ASSERT_FALSE(ast->m_left->m_right->m_right->m_right);
//
//     ASSERT_EQ(TermID::INTEGER, ast->m_right->getTermID());
//     ASSERT_EQ("123", ast->m_right->getText());
//
//     ASSERT_EQ("term.field1.field2 := 123;", ast->toString());
// }

TEST_F(ParserTest, ArrayAssign) {
    ASSERT_TRUE(Parse("$0[0] =  123;"));
    ASSERT_EQ(TermID::ASSIGN, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);

    ASSERT_EQ(TermID::INDEX, ast->m_left->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left->m_left);
    ASSERT_EQ(TermID::ARGUMENT, ast->m_left->m_left->getTermID());
    ASSERT_EQ("$0", ast->m_left->m_left->getText());

    ASSERT_EQ(1, ast->m_left->size());
    ASSERT_EQ("0", ast->m_left->at(0).second->getText());
}

TEST_F(ParserTest, DISABLED_ArrayAssign2) {
    ASSERT_TRUE(Parse("term[1][1..3] =  123;"));
    ASSERT_EQ(TermID::ASSIGN, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);

    ASSERT_EQ(TermID::NAME, ast->m_left->getTermID());
    ASSERT_EQ("term", ast->m_left->getText());

    ASSERT_TRUE(ast->m_left->m_right);
    ASSERT_EQ("[", ast->m_left->m_right->getText());
    ASSERT_TRUE(ast->m_left->m_right->m_right);
    ASSERT_EQ("[", ast->m_left->m_right->m_right->getText());
    ASSERT_FALSE(ast->m_left->m_right->m_right->m_right);

    ASSERT_EQ(TermID::INTEGER, ast->m_right->getTermID());
    ASSERT_EQ("123", ast->m_right->getText());

    ASSERT_EQ("term[1][1, 2, 3]=123;", ast->toString());
}

TEST_F(ParserTest, DISABLED_FieldArray) {
    ASSERT_TRUE(Parse("term.val[1].field :=  value[-1..@count()..5].field;"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term.val[1].field := value[-1..@count()..5].field;", ast->toString());
}

TEST_F(ParserTest, AssignSimple3) {
    ASSERT_TRUE(Parse("\t term   :=   term2(   )  ;  \n"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID()) << ast->toString();
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("term := term2();", ast->toString());
}

TEST_F(ParserTest, AssignSimpleArg) {
    ASSERT_TRUE(Parse("\t term    ::=    term2( arg  ) ;   \n"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ("term ::= term2(arg);", ast->toString());
}

TEST_F(ParserTest, AssignSimpleNamedArg) {
    ASSERT_TRUE(Parse("\t term  :=  $term2( arg = arg2  )\n;\n\n"));
    ASSERT_EQ("term := $term2(arg=arg2);", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs0) {
    ASSERT_TRUE(Parse("\t term   :=   \\term2( arg, arg1 = arg2  )  ;  \n"));
    ASSERT_EQ("term := \\term2(arg, arg1=arg2);", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs1) {
    ASSERT_TRUE(Parse("\t term   :=   @term2( arg, arg1 = arg2  )  ;  \n"));
    ASSERT_EQ("term := @term2(arg, arg1=arg2);", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs2) {
    ASSERT_TRUE(Parse("\t term   :=   $term2( arg, arg1 = arg2(arg3))  ;  \n"));
    ASSERT_EQ("term := $term2(arg, arg1=arg2(arg3));", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs3) {
    ASSERT_TRUE(Parse("\t $term   :=   term2( \\arg, arg1 = 123  )  ;  \n"));
    ASSERT_EQ("$term := term2(\\arg, arg1=123);", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs4) {
    ASSERT_TRUE(Parse("\t %term   :=   term2( arg, arg1 = \\arg2($arg3))  ;  \n"));
    ASSERT_EQ("%term := term2(arg, arg1=\\arg2($arg3));", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs5) {
    ASSERT_TRUE(Parse("\t %term   :=   term2( arg, arg1 = \\\\arg2($arg3))  ;  \n"));
    ASSERT_EQ("%term := term2(arg, arg1=\\\\arg2($arg3));", ast->toString());
}

TEST_F(ParserTest, AssignNamedArgs6) {
    ASSERT_TRUE(Parse("\t %term   :=   term2( arg, arg1 = @arg2($arg3))  ;  \n"));
    ASSERT_EQ("%term := term2(arg, arg1=@arg2($arg3));", ast->toString());
}

TEST_F(ParserTest, AssignString) {
    ASSERT_TRUE(Parse("term := \"строка\";"));
    ASSERT_EQ("term := \"строка\";", ast->toString());
}

TEST_F(ParserTest, AssignString2) {
    ASSERT_TRUE(Parse("$2  :=  \"строка\" ; \n"));
    ASSERT_EQ("$2 := \"строка\";", ast->toString());
}

TEST_F(ParserTest, AssignStringControlChar) {
    ASSERT_TRUE(Parse("$2 :=  \"стр\\\"\t\r\xffока\\s\" ; \n"));
    /* Esc-последовательности больше не декодируются лексером, сохраняются как есть */
    ASSERT_EQ("$2 := \"стр\\\"\t\r\xffока\\s\";", ast->toString());
}

TEST_F(ParserTest, AssignStringMultiline) {
    ASSERT_TRUE(Parse("term  :=  'стр\\\n\t  ока\\\n   \\s' ; \n"));
    /* Esc-последовательности больше не декодируются лексером, сохраняются как есть */
    ASSERT_EQ("term := 'стр\\\n\t  ока\\\n   \\s';", ast->toString());
}

TEST_F(ParserTest, AssignDictEmpty) {
    ASSERT_TRUE(Parse("term := (   ,    );"));
    ASSERT_EQ("term := (,);", ast->toString());
}

TEST_F(ParserTest, AssignDict) {
    ASSERT_TRUE(Parse("term := (name,)"));
    ASSERT_TRUE(ast);
    ASSERT_TRUE(ast->m_left);
    ASSERT_TRUE(ast->m_right);
    ASSERT_TRUE(ast->m_right->m_id == TermID::DICT);
    ASSERT_EQ("term := (name,);", ast->toString());

    ASSERT_TRUE(Parse("term := (  123 , )"));
    ASSERT_EQ("term := (123,);", ast->toString());

    ASSERT_TRUE(Parse("term := (  name  = 123 ,  )"));
    ASSERT_EQ("term := (name=123,);", ast->toString());
}

TEST_F(ParserTest, AssignArray) {
    ASSERT_TRUE(Parse("term := [  123  , ]"));
    ASSERT_EQ("term := [123,];", ast->toString());
}

TEST_F(ParserTest, ArgsArray1) {
    ASSERT_TRUE(Parse("term([1,]);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("term", ast->getText());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("[1,]", ast->at(0).second->toString());
}

TEST_F(ParserTest, LogicEq) {
    ASSERT_TRUE(Parse("var := 1==2;"));
    ASSERT_EQ("var := 1 == 2;", ast->toString());
}

TEST_F(ParserTest, LogicNe) {
    ASSERT_TRUE(Parse("var := 1!=2;"));
    ASSERT_EQ("var := 1 != 2;", ast->toString());
}

TEST_F(ParserTest, InstanceName) {
    ASSERT_TRUE(Parse("var ~ Class"));
    ASSERT_TRUE(Parse("var ~ :Class"));
    ASSERT_TRUE(Parse("var ~ 'name'"));
    ASSERT_TRUE(Parse("var ~ $var"));
    ASSERT_TRUE(Parse("1  ~  $var"));
    ASSERT_TRUE(Parse("'строка'  ~  'тип'"));
    ASSERT_TRUE(Parse("1..20 ~ var_name"));

    ASSERT_TRUE(Parse("var ~~ Class"));
    ASSERT_TRUE(Parse("var ~~ :Class"));
    ASSERT_TRUE(Parse("var ~~ 'name'"));
    ASSERT_TRUE(Parse("var ~~ $var"));
    ASSERT_TRUE(Parse("1  ~~  $var"));
    ASSERT_TRUE(Parse("'строка'  ~~  'тип'"));
    ASSERT_TRUE(Parse("1..20 ~~ var_name"));
}

TEST_F(ParserTest, FunctionSimple) {
    ASSERT_TRUE(Parse("func() := {{%%}};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func() := {{%%};};", ast->toString());
}

TEST_F(ParserTest, FunctionSimpleTwo) {
    ASSERT_TRUE(Parse("func() := {{% %};{% %}};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func() := {{% %}; {% %};};", ast->toString());
}

TEST_F(ParserTest, FunctionSimple2) {
    ASSERT_TRUE(Parse("func(arg)  :=  {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionSimple3) {
    ASSERT_TRUE(Parse("func(arg)  :=  {{%  %};{% %};{%  %}; $99:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg) := {{%  %}; {% %}; {%  %}; $99 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionSimple4) {
    ASSERT_TRUE(Parse("func(arg) := {$33:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func(arg) := {$33 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionSimple5) {
    ASSERT_TRUE(Parse("print(str=\"\") :={% printf(\"%s\", static_cast<char *>($str)); %};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("print(str=\"\") := {% printf(\"%s\", static_cast<char *>($str)); %};", ast->toString());
}

TEST_F(ParserTest, FunctionRussian1) {
    ASSERT_TRUE(Parse("мин(arg) := {$00:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg) := {$00 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionRussian2) {
    ASSERT_TRUE(Parse("мин(арг) := {$1:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(арг) := {$1 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionRussian3) {
    ASSERT_TRUE(Parse("русс(10,20);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("русс(10, 20)", ast->toString());
}

TEST_F(ParserTest, FunctionRussian4) {
    ASSERT_TRUE(Parse("мин(10,20);"));
    ASSERT_EQ(TermID::NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(10, 20)", ast->toString());
}

TEST_F(ParserTest, FunctionArgs) {
    ASSERT_TRUE(Parse("мин(...) := {$1:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(...) := {$1 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionArgs2) {
    ASSERT_TRUE(Parse("мин(arg, ...) := {$1:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg, ...) := {$1 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionArgs3) {
    ASSERT_TRUE(Parse("мин(arg1, arg2, ...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg1, arg2, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionKwArgs1) {
    ASSERT_TRUE(Parse("мин(...) := {$0:=0;func();var;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(...) := {$0 := 0; func(); var;};", ast->toString());
}

TEST_F(ParserTest, FunctionKwArgs2) {
    ASSERT_TRUE(Parse("мин(arg=123 ,  ...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg=123, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionKwArgs3) {
    ASSERT_TRUE(Parse("мин(arg1=1, arg2=2 ,...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg1=1, arg2=2, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionArgsAll) {
    ASSERT_TRUE(Parse("мин(arg1=1, arg2=2 , ...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg1=1, arg2=2, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionArgsAll2) {
    ASSERT_TRUE(Parse("мин(arg, arg1=1, arg2=2, ...) := {$0:=0;};"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("мин(arg, arg1=1, arg2=2, ...) := {$0 := 0;};", ast->toString());
}

TEST_F(ParserTest, FunctionEmpty) {
    ASSERT_TRUE(Parse("мин(arg, arg1=1, arg2=2, ...) := {};"));
    ASSERT_EQ("мин(arg, arg1=1, arg2=2, ...) := {};", ast->toString());
}

TEST_F(ParserTest, FunctionArgsFail) {
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(... ...) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(arg ...) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(arg=1 ..., arg) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(arg=1, arg ...) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("мин(arg=1 ...) := {$0:=0;};"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(ParserTest, ArrayAdd7) {
    ASSERT_TRUE(Parse("name()  :=  term2;")); // $[].name:=term2;
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("name() := term2;", ast->toString());
}

TEST_F(ParserTest, Ellipsis0) {
    ASSERT_TRUE(Parse("... = _;")); //
    ASSERT_EQ("...=_;", ast->toString());

    ASSERT_TRUE(Parse("... = name;")); //
    ASSERT_EQ("...=name;", ast->toString());

    ASSERT_TRUE(Parse("... = name, name2::, ::na::name3;")); //
    ASSERT_EQ("...=name,name2::,::na::name3;", ast->toString());
}

TEST_F(ParserTest, Ellipsis1) {
    ASSERT_TRUE(Parse("name  :=  term2(arg1  ,  ...    ...    dict);")); //
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("name := term2(arg1, ... ...dict);", ast->toString());
}

// TEST_F(ParserTest, DISABLED_Complex1) {
//     ASSERT_TRUE(Parse("10+20j"));
//     ASSERT_EQ(TermID::COMPLEX, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ("10+20j;", ast->toString());
// }
//
// TEST_F(ParserTest, DISABLED_Complex2) {
//     ASSERT_TRUE(Parse("0j"));
//     ASSERT_EQ(TermID::COMPLEX, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ("0j;", ast->toString());
// }
//
// TEST_F(ParserTest, DISABLED_Complex3) {
//     ASSERT_TRUE(Parse("0.1-0.20j"));
//     ASSERT_EQ(TermID::COMPLEX, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ("0.1-0.20j;", ast->toString());
// }

TEST_F(ParserTest, Rational) {
    ASSERT_TRUE(Parse("1\\1"));
    ASSERT_EQ(TermID::RATIONAL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("1\\1", ast->toString());
}

TEST_F(ParserTest, Rational2) {
    ASSERT_TRUE(Parse("1\\-20"));
    ASSERT_EQ(TermID::RATIONAL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("1\\-20", ast->toString());
}

TEST_F(ParserTest, Rational3) {
    ASSERT_TRUE(Parse("-3\\11"));
    ASSERT_EQ(TermID::RATIONAL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("-3\\11", ast->toString());
}

TEST_F(ParserTest, ArrayAdd9) {
    ASSERT_TRUE(Parse("$name  :=  term2"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("$name := term2;", ast->toString());
}

TEST_F(ParserTest, Ellipsis2) {
    ASSERT_TRUE(Parse("\\name  :=  term2(   ...   arg);"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("\\name := term2(...arg);", ast->toString());
}

TEST_F(ParserTest, Func1) {
    ASSERT_TRUE(Parse("func_arg(arg1 :Int8, arg2) :Int8 := { $arg1+$arg2; };"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func_arg(arg1:Int8, arg2):Int8 := {$arg1 + $arg2;};", ast->toString());
}

TEST_F(ParserTest, Func2) {
    ASSERT_TRUE(Parse("func_arg(arg1:&Int8, &arg2) :&Int8 := { $arg1+$arg2; };"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("func_arg(arg1:&Int8, &arg2):&Int8 := {$arg1 + $arg2;};", ast->toString());
}

TEST_F(ParserTest, Func3) {
    ASSERT_TRUE(Parse("$res:Int8 ::= func_arg(100, 100);"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("$res:Int8 ::= func_arg(100, 100);", ast->toString());
}

TEST_F(ParserTest, Func4) {
    ASSERT_TRUE(Parse("res() := func_arg(100, 100); res() := func_arg(100, 100); res() := func_arg(100, 100);"));
}

TEST_F(ParserTest, Comment) {
    ASSERT_TRUE(Parse("#!line1"));
    ASSERT_TRUE(Parse("#!line1\n"));
    ASSERT_TRUE(Parse("#!line1\n#!line2"));
    ASSERT_TRUE(Parse("#!line1\n#!line2\n\n#!line4"));
    // ASSERT_EQ(TermID::COMMENT, ast->getTermID()) << EnumStr(ast->getTermID());
}

// TEST_F(ParserTest, Comment2) {
//     ASSERT_TRUE(Parse("#!line1\n#line2\n \n\n/* \n \n */ \n"));
//     //    ASSERT_EQ(TermID::BLOCK, ast->getTermID()) << EnumStr(ast->getTermID());
//     //    ASSERT_EQ(3, ast->m_sequence.size());
//     //    ASSERT_EQ(TermID::COMMENT, ast->m_sequence[0]->getTermID()) << EnumStr(ast->getTermID());
//     //    ASSERT_EQ(TermID::COMMENT, ast->m_sequence[1]->getTermID()) << EnumStr(ast->getTermID());
//     //    ASSERT_EQ(TermID::COMMENT, ast->m_sequence[2]->getTermID()) << EnumStr(ast->getTermID());
// }

// TEST_F(ParserTest, Comment3) {
//     const char* str = "print(str=\"\") := { {%  %} };\n"
//                       "#!/bin/nlc;\n"
//                       "\n"
//                       "\n"
//                       "# @print(\"Привет, мир!\\n\");\n";
//     ASSERT_TRUE(Parse(str));
//     ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
//     //    ASSERT_EQ(3, ast->m_sequence.size());
//     //    ASSERT_EQ(TermID::FUNCTION, ast->m_sequence[0]->getTermID())<< EnumStr(ast->getTermID());
//     //    ASSERT_EQ(TermID::COMMENT, ast->m_sequence[1]->getTermID())<< EnumStr(ast->getTermID());
//     //    ASSERT_EQ(TermID::COMMENT, ast->m_sequence[2]->getTermID())<< EnumStr(ast->getTermID());
// }

// TEST_F(ParserTest, Comment4) {
//     const char* str = "#!/bin/nlc;\n"
//                       "print1(str=\"\") := {%  %};\n"
//                       "print2(str=\"\") := {%  %};\n"
//                       "# @print(\"Привет, мир!\\n\");\n";
//     ASSERT_TRUE(Parse(str));
//     ASSERT_EQ(TermID::SEQUENCE, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ(2, ast->m_sequence.size());
//     //    ASSERT_EQ(TermID::COMMENT, ast->m_sequence[0]->getTermID()) << EnumStr(ast->getTermID());
//     ASSERT_EQ(TermID::CREATE_NAME, ast->m_sequence[0]->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ(TermID::CREATE_NAME, ast->m_sequence[1]->getTermID()) << trust::toString(ast->getTermID());
//     //    ASSERT_EQ(TermID::COMMENT, ast->m_sequence[2]->getTermID())<< EnumStr(ast->getTermID());
// }

// TEST_F(ParserTest, Comment5) {
//     const char* str = "term();\n"
//                       "print1(str=\"\") := {%  %};\n"
//                       "print2(str=\"\") := { {%  %} };\n\n"
//                       "print3(str=\"\") := {%  %};\n\n\n"
//                       "# @print(\"Привет, мир!\\n\");\n";
//     ASSERT_TRUE(Parse(str));
//     ASSERT_EQ(TermID::SEQUENCE, ast->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ(4, ast->m_sequence.size());
//     ASSERT_EQ(TermID::NAME, ast->m_sequence[0]->getTermID()) << trust::toString(ast->getTermID());

//     ASSERT_EQ(TermID::CREATE_NAME, ast->m_sequence[1]->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ(TermID::CREATE_NAME, ast->m_sequence[2]->getTermID()) << trust::toString(ast->getTermID());
//     ASSERT_EQ(TermID::CREATE_NAME, ast->m_sequence[3]->getTermID()) << trust::toString(ast->getTermID());
// }

// TEST_F(ParserTest, Comment6) {
//     ASSERT_TRUE(Parse("# @@macro @@@"));
//     ASSERT_FALSE(ast->size());
//     ASSERT_TRUE(Parse("# @@macro @@@\n"));
//     ASSERT_FALSE(ast->size());
//     ASSERT_TRUE(Parse("/* @@macro @@@ */"));
//     ASSERT_FALSE(ast->size());
//     ASSERT_TRUE(Parse("/* @@macro @@@\n*/"));
//     ASSERT_FALSE(ast->size());
//     ASSERT_TRUE(Parse("/* @@macro @@@\n\n*/"));
//     ASSERT_FALSE(ast->size());
//     ASSERT_TRUE(Parse("/*/* @@macro @@@\n\n*/*/"));
//     ASSERT_FALSE(ast->size());
// }

// TEST_F(ParserTest, CommentIncluded) {
//     //    const char *str = "/* !!!!!!! \n"
//     //            "@print(\"Привет, мир!\\n\");\n*/";
//     //    "# @print(\"Привет, мир!\\n\");\n";
//     //    ASSERT_TRUE(Parse(str));

//     const char* str2 = "/* /* /* /* term();\n"
//                        "print1(str=\"\") ::= term();\n"
//                        "print2(str=\"\") ::= term();\n\n */ "
//                        "print3( */ str=\"\") ::= term();\n\n\n"
//                        "ddd  */  "
//                        "# @print(\"Привет, мир!\\n\");\n";
//     ASSERT_TRUE(Parse(str2));
// }

TEST_F(ParserTest, Types) {
    EXPECT_TRUE(Parse(":type1 := :type;"));
    EXPECT_TRUE(Parse(":type2 := :type;"));
    EXPECT_TRUE(Parse(":type3 := type();"));
    EXPECT_TRUE(Parse(":type4 := type();;"));

    //    EXPECT_TRUE(Parse(":type5 ::= ()"));
    //    EXPECT_TRUE(Parse(":type6 ::= ();"));
    EXPECT_TRUE(Parse(":type7 ::= :Type;"));
    EXPECT_TRUE(Parse(":type8 ::= :Type();;"));

    EXPECT_TRUE(Parse(":type9 := (1234,);"));
    EXPECT_TRUE(Parse(":type10 := (1234, name=1234,);"));
    EXPECT_TRUE(Parse(":type11 := class1(1234);"));
    EXPECT_TRUE(Parse(":type12 := :class1(1234, name=1234);;"));

    //    EXPECT_TRUE(Parse(":type13 := class1(), class2()"));
    //    EXPECT_TRUE(Parse(":type14 := class1(), class2(), (name=value);"));
    //    EXPECT_TRUE(Parse(":type15 := :class1, :class1(arg, arg=222)"));
    //    EXPECT_TRUE(Parse(":type16 := class1(args), (extra,), (extra=222,);"));

    EXPECT_TRUE(Parse(":type17 := class(1234);"));
    //    EXPECT_TRUE(Parse(":type18 := class(name=1234), class2();"));
    //    EXPECT_TRUE(Parse(":type19 := class(name=1234), class2()"));
    EXPECT_TRUE(Parse(":type20 := (1234, name=1234,);"));

    EXPECT_TRUE(Parse(":*type1 := :type;"));
    EXPECT_TRUE(Parse(":*type2 := :*type;"));
    EXPECT_TRUE(Parse(":*type3 := type();"));
    EXPECT_TRUE(Parse(":*type4 := *type();;"));

    EXPECT_TRUE(Parse(":type"));
    ASSERT_EQ(TermID::TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(":type", ast->getText()) << ast->getText();
    ASSERT_EQ(":type", ast->toString()) << ast->toString();

    EXPECT_TRUE(Parse(":*type"));
    // take - узел-оператор (TAKE) с правым операндом TYPE ":type".
    ASSERT_EQ(TermID::TAKE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("*", ast->getText()) << ast->getText();
    ASSERT_TRUE(ast->m_right);
    ASSERT_EQ(TermID::TYPE, ast->m_right->getTermID()) << trust::toString(ast->m_right->getTermID());
    ASSERT_EQ(":type", ast->m_right->getText()) << ast->m_right->getText();
    ASSERT_EQ(":*type", ast->toString()) << ast->toString();
}

TEST_F(ParserTest, Const2) {
    ASSERT_TRUE(Parse("const^  ::=   \"CONST\";"));
    ASSERT_EQ(TermID::CREATE_TYPE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("const^ ::= \"CONST\";", ast->toString());
}

TEST_F(ParserTest, Const3) {
    ASSERT_TRUE(Parse("const^  :=   123;"));
    ASSERT_EQ(TermID::CREATE_NAME, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ("const^ := 123;", ast->toString());
}

TEST_F(ParserTest, Sequence) {
    ASSERT_NO_THROW(Parse(";"));
    ASSERT_NO_THROW(Parse(";;"));
    ASSERT_NO_THROW(Parse(";;;"));
    ASSERT_NO_THROW(Parse("val;"));
    ASSERT_NO_THROW(Parse("val;val;"));

    ASSERT_NO_THROW(Parse("val;;"));
    ASSERT_NO_THROW(Parse("val;;;"));
    ASSERT_NO_THROW(Parse("val;;;;val;;;;"));

    ASSERT_NO_THROW(Parse("val();"));
    ASSERT_NO_THROW(Parse("val();val();"));

    ASSERT_NO_THROW(Parse("val();"));
    ASSERT_NO_THROW(Parse("val();;;"));
    ASSERT_NO_THROW(Parse("val();;;val();;;"));

    //    ASSERT_NO_THROW(Parse("(){};"));
    ASSERT_NO_THROW(Parse("_()::={val;};"));
    ASSERT_NO_THROW(Parse("_()::={val;val;};"));

    ASSERT_NO_THROW(Parse("_()::={val();};"));
    ASSERT_NO_THROW(Parse("_()::={val();val();};"));
    ASSERT_NO_THROW(Parse("_()::={val();val();;};"));

    ASSERT_NO_THROW(Parse("_()::={val();};"));
    ASSERT_NO_THROW(Parse("_()::={val();;;};;;"));
    ASSERT_NO_THROW(Parse("_()::= {val();;;val();;;};;;"));
    ASSERT_NO_THROW(Parse("_()::= {val();;;_()::={val();};;;;};;;"));
}

TEST_F(ParserTest, BlockTry) {
    ASSERT_TRUE(Parse("_()::={*1; 2; 3;*}; 4;"));
    ASSERT_EQ(2, ast->m_sequence.size());
    ASSERT_EQ(TermID::SEQUENCE, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(TermID::CREATE_TYPE, ast->m_sequence[0]->getTermID()) << trust::toString(ast->m_sequence[0]->getTermID());
    ASSERT_EQ(TermID::INTEGER, ast->m_sequence[1]->getTermID()) << trust::toString(ast->m_sequence[1]->getTermID());

    ASSERT_TRUE(Parse("_():={- 1; 2; 3; --4--; 5; 6;-}; 100;"));
    ASSERT_EQ(2, ast->m_sequence.size());
    ASSERT_EQ(TermID::CREATE_NAME, ast->m_sequence[0]->getTermID()) << trust::toString(ast->m_sequence[0]->getTermID());
    ASSERT_EQ(TermID::INTEGER, ast->m_sequence[1]->getTermID()) << trust::toString(ast->m_sequence[1]->getTermID());
}

TEST_F(ParserTest, Repeat) {
    ASSERT_TRUE(Parse("[val] <-> val;"));
    ASSERT_TRUE(Parse("val <-> [val];;"));
    ASSERT_TRUE(Parse("[val] <-> {val;};"));
    ASSERT_TRUE(Parse("[val] <-> {val;};;"));
    ASSERT_TRUE(Parse("[val] <-> {val;};"));
    ASSERT_TRUE(Parse("[val] <-> {val;};;"));

    ASSERT_TRUE(Parse("val <-> [val()];"));
    ASSERT_TRUE(Parse("val <-> [val()];;"));
    ASSERT_TRUE(Parse("[val()] <-> {val();};"));
    ASSERT_TRUE(Parse("[val()] <-> {val();};;"));
    ASSERT_TRUE(Parse("[val()] <-> {val();};"));
    ASSERT_TRUE(Parse("[val()] <-> {val();};;"));

    ASSERT_TRUE(Parse("[val()] <-> {val()};val();"));
    ASSERT_TRUE(Parse("val <-> [val()];val();"));
    ASSERT_TRUE(Parse("[val()] <-> {val();val();};"));
    ASSERT_TRUE(Parse("[val()] <-> {val();val();};;"));
    ASSERT_TRUE(Parse("[val()] <-> {val();val();};"));
    ASSERT_TRUE(Parse("[val()] <-> {val();val();};;"));
}

TEST_F(ParserTest, Else) {
    ASSERT_TRUE(Parse("[val] <-> val, [...]-->else;"));
    ASSERT_TRUE(Parse("[val] <->{val}, [...]-->{else}"));
}

TEST_F(ParserTest, BlockThenStmt) {
    // Блок (анонимный/именованный) как первый элемент последовательности, за которым идёт оператор.
    ASSERT_TRUE(Parse("{ x := 1; }; y := 2;"));
    ASSERT_TRUE(Parse("{ x := 1; }; { y := 2; };"));
    ASSERT_TRUE(Parse("outer:: { x := 1; }; y := 2;"));
}

TEST_F(ParserTest, CheckResult) {

    ASSERT_TRUE(Parse("{ expr }; "));
    ASSERT_TRUE(Parse("{- expr -}"));
    ASSERT_TRUE(Parse("{+ expr +}"));
    ASSERT_TRUE(Parse("{* expr *}"));

    ASSERT_TRUE(Parse("{ expr }; "));
    ASSERT_TRUE(Parse("** {- expr -}"));
    ASSERT_TRUE(Parse("** {+ expr +}"));
    ASSERT_TRUE(Parse("** {* expr *}"));

    ASSERT_TRUE(Parse("{ ++expr++ }; "));
    ASSERT_TRUE(Parse("{- +-expr-+ -}"));
    ASSERT_TRUE(Parse("{+ --expr-- +}"));
    ASSERT_TRUE(Parse("{* --expr-- *}"));

    ASSERT_TRUE(Parse("{ ++100++ }; "));
    ASSERT_TRUE(Parse("{- --100-- -}"));
    ASSERT_TRUE(Parse("{+ --100-- +}"));
    ASSERT_TRUE(Parse("{* --100-- *}"));

    ASSERT_TRUE(Parse("{ expr } :Type; "));
    ASSERT_TRUE(Parse("{- expr -} :Type "));
    ASSERT_TRUE(Parse("{+ expr +} :Type "));
    ASSERT_TRUE(Parse("{* expr *} :Type "));

    //    ASSERT_FALSE(Parse("{ expr }: Type "));
    //    ASSERT_FALSE(Parse("{- expr -}: Type "));
    //    ASSERT_FALSE(Parse("{+ expr +}: Type "));
    //    ASSERT_FALSE(Parse("{* expr *}: Type "));

    ASSERT_TRUE(Parse("{ expr }:<:Type, :Type, > "));
    ASSERT_TRUE(Parse("{- expr -}:<:Type, :Type, > "));
    ASSERT_TRUE(Parse("{+ expr +}:<:Type, :Type, >; "));
    ASSERT_TRUE(Parse("{* expr *}:<:Type, :Type, >;; "));

    ASSERT_TRUE(Parse("{ expr }:<..., > "));
    ASSERT_TRUE(Parse("{- expr -}:<..., > "));
    ASSERT_TRUE(Parse("{+ expr +}:<..., >; "));
    ASSERT_TRUE(Parse("{* expr *} : <  ...,  >;; "));
}

TEST_F(ParserTest, WithArgs) {
    // with (a, b) { body } - аргументы собираются в m_args через общее правило args
    ASSERT_TRUE(Parse("** (a, b) { expr }"));
    ASSERT_EQ(TermID::WITH, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("a", ast->at(0).second->getText());
    ASSERT_EQ("b", ast->at(1).second->getText());

    // Именованный аргумент
    ASSERT_TRUE(Parse("** (x = 1) { expr }"));
    ASSERT_EQ(TermID::WITH, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("x", ast->at(0).first);
    ASSERT_EQ("1", ast->at(0).second->m_right->getText());

    // Пустые аргументы
    ASSERT_TRUE(Parse("** () { expr }"));
    ASSERT_EQ(TermID::WITH, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(0, ast->size());
}

TEST_F(ParserTest, WhenArgs) {
    // when_all (a, b)
    ASSERT_TRUE(Parse("[**] (a, b)"));
    ASSERT_EQ(TermID::WHEN_ALL, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(2, ast->size());
    ASSERT_EQ("a", ast->at(0).second->getText());
    ASSERT_EQ("b", ast->at(1).second->getText());

    // Именованный аргумент в when_any
    ASSERT_TRUE(Parse("[***] (x = 1)"));
    ASSERT_EQ(TermID::WHEN_ANY, ast->getTermID()) << trust::toString(ast->getTermID());
    ASSERT_EQ(1, ast->size());
    ASSERT_EQ("x", ast->at(0).first);
    ASSERT_EQ("1", ast->at(0).second->m_right->getText());
}

TEST_F(ParserTest, OperInt) {
    ASSERT_TRUE(Parse("_()::={val * val}"));
    ASSERT_TRUE(Parse("_()::={+val * val+}"));
    ASSERT_TRUE(Parse("_():Void::={- val * val -}"));
    ASSERT_TRUE(Parse("_():Void ::= {*val * val*}"));
    ASSERT_TRUE(Parse("_()::= {val * val}"));
    ASSERT_TRUE(Parse("_()::= {+val * val+}"));
    ASSERT_TRUE(Parse("_()::= {-val * val-}"));
    ASSERT_TRUE(Parse("_()::= {*val * val*}"));
}

TEST_F(ParserTest, Operators) {
    ASSERT_TRUE(Parse("val + val;"));
    ASSERT_TRUE(Parse("val - val;"));
    ASSERT_TRUE(Parse("_()::={val * val;};"));
    ASSERT_TRUE(Parse("_()::={val / val;};"));

    ASSERT_TRUE(Parse("val + val();"));
    ASSERT_TRUE(Parse("val - val();;"));
    ASSERT_TRUE(Parse("val * val();;"));
    ASSERT_TRUE(Parse("_()::={val / val();};"));

    ASSERT_TRUE(Parse("val + val();"));
    ASSERT_TRUE(Parse("val - val();; val - val();"));
    ASSERT_TRUE(Parse("val * val();; val - val();;"));
    ASSERT_TRUE(Parse("_()::={val / val(); val - val()};;"));

    ASSERT_TRUE(Parse("(val + val());"));
    ASSERT_TRUE(Parse("(val - val()) + (val - val());"));
    ASSERT_TRUE(Parse("(val * val()) - (val - val());"));
    ASSERT_TRUE(Parse("_()::={val / val() + (val - val())};"));
    ASSERT_TRUE(Parse("_()::= {* val * val() / (val - val()); *} :Void;"));

    ASSERT_TRUE(Parse("val + [1,2,];"));
    ASSERT_TRUE(Parse("val * [1,]:Int8;"));
    ASSERT_TRUE(Parse("val ~ (,)"));
    ASSERT_TRUE(Parse("val ~ (,):Class"));
}

TEST_F(ParserTest, Repeat0) {
    ASSERT_TRUE(Parse("[human || $ttttt  &&  123    !=    test[0].field ] <-> if_1;"));
    ASSERT_EQ("[human || $ttttt && 123 != test[0].field]<->if_1;", ast->toString());
}

TEST_F(ParserTest, Repeat1) {
    ASSERT_TRUE(Parse("[test == human] <-> if_1;"));
    ASSERT_EQ("[test == human]<->if_1;", ast->toString());
}

TEST_F(ParserTest, Repeat2) {
    ASSERT_TRUE(Parse("[test != human] <-> {if_1;};"));
    ASSERT_EQ("[test != human]<->{if_1;};", ast->toString());

    ASSERT_TRUE(Parse("{if_1;} <-> [test!=human];"));
    ASSERT_EQ("{if_1;}<->[test != human];", ast->toString());
}

TEST_F(ParserTest, Repeat3) {
    ASSERT_TRUE(Parse("[test != human] <-> {if_1;if_2;then3;};"));
    ASSERT_EQ("[test != human]<->{if_1; if_2; then3;};", ast->toString());

    ASSERT_TRUE(Parse("{if_1;if_2;then3;} <-> [test != human];"));
    ASSERT_EQ("{if_1; if_2; then3;}<->[test != human];", ast->toString());
}

TEST_F(ParserTest, Repeat4) {
    ASSERT_TRUE(Parse("[test()] <-> {if_1;if_2;then3;};"));
    ASSERT_EQ("[test()]<->{if_1; if_2; then3;};", ast->toString());

    ASSERT_TRUE(Parse("[test()] <-> {if_1;if_2;then3;};"));
    ASSERT_EQ("[test()]<->{if_1; if_2; then3;};", ast->toString());

    ASSERT_TRUE(Parse("{if_1;if_2;then3;} <->  [test()];"));
    ASSERT_EQ("{if_1; if_2; then3;}<->[test()];", ast->toString());

    ASSERT_TRUE(Parse("{if_1;if_2;then3;} <->  [test()];"));
    ASSERT_EQ("{if_1; if_2; then3;}<->[test()];", ast->toString());
}

TEST_F(ParserTest, Repeat5) {
    ASSERT_TRUE(Parse(" [ test! ]<-> {if_1;if_2;then3;};"));
    ASSERT_EQ("[test!]<->{if_1; if_2; then3;};", ast->toString());

    ASSERT_TRUE(Parse(" [test!] <-> {if_1;if_2;then3;};"));
    ASSERT_EQ("[test!]<->{if_1; if_2; then3;};", ast->toString());

    ASSERT_TRUE(Parse("{if_1;if_2;then3;}<->[test!];"));
    ASSERT_EQ("{if_1; if_2; then3;}<->[test!];", ast->toString());

    ASSERT_TRUE(Parse("{if_1;if_2;then3;}<->[test!];"));
    ASSERT_EQ("{if_1; if_2; then3;}<->[test!];", ast->toString());
}

TEST_F(ParserTest, Repeat6) {
    ASSERT_TRUE(Parse("[test!] <-> {if_1;if_2;then3;};"));
    ASSERT_EQ("[test!]<->{if_1; if_2; then3;};", ast->toString());
}

TEST_F(ParserTest, Repeat7) {
    ASSERT_TRUE(Parse("[test[0].field != $test!] <-> if_1;"));
    ASSERT_EQ("[test[0].field != $test!]<->if_1;", ast->toString());
}

TEST_F(ParserTest, Range) {
    ASSERT_TRUE(Parse("0.1..0.9..0.1;"));
    ASSERT_EQ("0.1..0.9..0.1", ast->toString());
}

TEST_F(ParserTest, Range1) {
    ASSERT_TRUE(Parse("[i!] <-> call();"));
    ASSERT_EQ("[i!]<->call();", ast->toString());

    ASSERT_TRUE(Parse("call() <-> [i!];"));
    ASSERT_EQ("call()<->[i!];", ast->toString());
}

TEST_F(ParserTest, Range2) {
    ASSERT_TRUE(Parse("[i()] <-> @error(\"Error\");"));
    ASSERT_EQ("[i()]<->@error(\"Error\");", ast->toString());
}

TEST_F(ParserTest, RangeCall) {
    ASSERT_TRUE(Parse("0.1..$sss"));
    ASSERT_TRUE(Parse("0.1..1*2"));
    ASSERT_TRUE(Parse("0.1..term()"));
    ASSERT_TRUE(Parse("0.1..term()..1*2+2-term"));
    ASSERT_TRUE(Parse("$term..term()+$term..-1*2+2-@term()"));
}

TEST_F(ParserTest, Follow) {
    //@todo Не получается сделать парсер с простым if, т.к. требуется вторая закрывающая точка с запятой
    ASSERT_TRUE(Parse("[test     ==    human    ||    ttttt&&123!=test.field ] --> if_1;"));
    ASSERT_EQ("[test == human || ttttt && 123 != test.field]-->if_1;", ast->toString());
}

TEST_F(ParserTest, Follow0) {
    ASSERT_TRUE(Parse("[test] --> follow;"));
    ASSERT_EQ("[test]-->follow;", ast->toString());

    ASSERT_TRUE(Parse("[test] --> follow;"));
    ASSERT_EQ("[test]-->follow;", ast->toString());

    ASSERT_TRUE(Parse("[test] --> {follow;};"));
    ASSERT_EQ("[test]-->{follow;};", ast->toString());

    ASSERT_TRUE(Parse("[test] --> {follow};"));
    ASSERT_EQ("[test]-->{follow;};", ast->toString());

    ASSERT_TRUE(Parse("[test] --> {follow;};"));
    ASSERT_EQ("[test]-->{follow;};", ast->toString());

    ASSERT_TRUE(Parse("[test] --> {follow;};"));
    ASSERT_EQ("[test]-->{follow;};", ast->toString());
}

TEST_F(ParserTest, Follow1) {
    //@todo Не получается сделать парсер с простым if, т.к. требуется вторая закрывающая точка с запятой
    ASSERT_TRUE(Parse("[test == human] --> if_1;"));
    ASSERT_EQ("[test == human]-->if_1;", ast->toString());
}

TEST_F(ParserTest, Follow2) {
    ASSERT_TRUE(Parse("[test != human] --> {if_1;};"));
    ASSERT_EQ("[test != human]-->{if_1;};", ast->toString());
}

TEST_F(ParserTest, Follow3) {
    ASSERT_TRUE(Parse("[test!=human] --> {if_1;if_2;then3;};"));
    ASSERT_EQ("[test != human]-->{if_1; if_2; then3;};", ast->toString());
}

TEST_F(ParserTest, Follow4) {
    ASSERT_TRUE(Parse("[test != human] --> {if_1;if_2;then3;}, [...] --> {else;};"));
    //    ASSERT_EQ("(test!=human)->{if_1; if_2; then3;},\n (_) ->{else;};", ast->toString());
}

TEST_F(ParserTest, Follow5) {
    ASSERT_TRUE(Parse("[@test1('')] --> {then1;}, [$test2] --> {then2;then2;}, [@test3+$test3] --> {then3;};"));
    //    ASSERT_EQ("(@test1)->{then1;}\n ($test2)->{then2; then2;}\n(@test3+$test3)->{then3;};", ast->toString());
}

TEST_F(ParserTest, Follow6) {
    ASSERT_TRUE(Parse("[@test1()] --> {then1;}, [$test2] --> {then2;}, [@test3+$test3] --> {then3;}, [...] --> {else;else();};"));
    //    ASSERT_EQ("(@test1)->{then1;},\n ($test2)->{then2;},\n (@test3+$test3)->{then3;},\n _ ->{else; else();};", ast->toString());
}

TEST_F(ParserTest, DISABLED_Follow7) {
    ASSERT_TRUE(Parse("[test.field[0] > iter!!] --> if_1;"));
    //    ASSERT_EQ("(test.field[0].field2>iter!!)->{if_1;};", ast->toString());
}

/*
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


    {*   # try:
        a = float(input("Введите число: ");
        print (100 / a);
 *} ~> {

        [:ValueError] --> print ("Это не число!"),                  # except ValueError:
        [:ZeroDivisionError] --> print ("На ноль делить нельзя!"),  # except ZeroDivisionError
        [:IntMinus] --> print ("Неожиданная ошибка."),              # except:
        [...] --> print ("Код выполнился без ошибок");                # else:

        print ("Я выполняюсь в любом случае!");                     # finally:
    };
 */

TEST_F(ParserTest, Match1) {
    ASSERT_TRUE(Parse("[1]==>{[1]-->first;}"));
}

TEST_F(ParserTest, Match1_0) {
    ASSERT_TRUE(Parse("[1]==>{[1]-->{first};}"));
}

TEST_F(ParserTest, Match2) {
    ASSERT_TRUE(Parse("[1]==>{[1]-->first;[2]-->second();}"));
}

TEST_F(ParserTest, Match2_0) {
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("[1]==>{[1]-->first;[...]-->end();[2]-->second();}"));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(ParserTest, Match3) {
    ASSERT_TRUE(Parse("[1]==>{[1]-->first;[2]-->second();[...]-->end();}"));
}

TEST_F(ParserTest, Match3_0) {
    ASSERT_TRUE(Parse("[1]==>{[1]-->first;[2]-->{second();second()};[...]-->{end()};}"));
}

TEST_F(ParserTest, Error) {
    ASSERT_TRUE(Parse("--;"));
    ASSERT_TRUE(Parse("--;;"));
    ASSERT_TRUE(Parse("ns:: --;"));
    ASSERT_TRUE(Parse("ns:: --;;"));
    ASSERT_TRUE(Parse("ns::ns:: --;"));
    ASSERT_TRUE(Parse("ns::ns:: --;;"));
    ASSERT_TRUE(Parse(":: --;"));
    ASSERT_TRUE(Parse(":: --;;"));
    ASSERT_TRUE(Parse("::ns --;"));
    ASSERT_TRUE(Parse("::ns --;;"));
    ASSERT_TRUE(Parse("::ns:: --;"));
    ASSERT_TRUE(Parse("::ns:: --;;"));
    ASSERT_TRUE(Parse("--_--;"));
    ASSERT_TRUE(Parse("--_--;;"));
    ASSERT_TRUE(Parse("--  eval --;"));
    ASSERT_TRUE(Parse("--eval--;;"));
    ASSERT_TRUE(Parse("--eval()--;"));
    ASSERT_TRUE(Parse("--  eval() --;;"));
    ASSERT_TRUE(Parse("--0--;"));
    ASSERT_TRUE(Parse("--0--;;"));
    ASSERT_TRUE(Parse("ns:: --0--;"));
    ASSERT_TRUE(Parse("ns:: --0--;;"));
    ASSERT_TRUE(Parse("++  0.1 ++;"));
    ASSERT_TRUE(Parse("--  0.1    --;;"));
    ASSERT_TRUE(Parse("name ++  0.1 ++;"));
    ASSERT_TRUE(Parse("name --  0.1    --;;"));
    ASSERT_TRUE(Parse("--[0,]--;"));
    ASSERT_TRUE(Parse("++(0,)++;;"));
    ASSERT_TRUE(Parse("--(0,1,2,3,):Class--;;"));
    ASSERT_TRUE(Parse("--(0,2,3,) :Class --;;"));
    ASSERT_TRUE(Parse("--(0,)--;;"));
    ASSERT_TRUE(Parse("--:Class(var)--;"));
    ASSERT_TRUE(Parse("--:Class[0](1)--;"));
    ASSERT_TRUE(Parse("--call()--;"));
    ASSERT_TRUE(Parse("--call()--;;"));
    ASSERT_TRUE(Parse("--call(arg)--;"));
    ASSERT_TRUE(Parse("--call(arg)--;;"));
    ASSERT_TRUE(Parse("--:class--;"));
    ASSERT_TRUE(Parse("--:class--;;"));
    ASSERT_TRUE(Parse("--:class()--;"));
    ASSERT_TRUE(Parse("--:class()--;;"));
    ASSERT_TRUE(Parse("--:class(arg)--;"));
    ASSERT_TRUE(Parse("++:class(arg)++;;"));
    ASSERT_TRUE(Parse("ns:: --:class--;"));
    ASSERT_TRUE(Parse("ns:: --:class--;;"));
    ASSERT_TRUE(Parse("ns::ns:: --:class--;"));
    ASSERT_TRUE(Parse("ns::ns:: --:class--;;"));
    ASSERT_TRUE(Parse(":: --:class--;"));
    ASSERT_TRUE(Parse(":: --:class--;;"));
    ASSERT_TRUE(Parse("::ns --:class--;"));
    ASSERT_TRUE(Parse("::ns --:class--;;"));
    ASSERT_TRUE(Parse("::ns:: --:class--;"));
    ASSERT_TRUE(Parse("::ns:: --:class--;;"));

    ASSERT_TRUE(Parse("++;"));
    ASSERT_TRUE(Parse("++;;"));
    ASSERT_TRUE(Parse("ns:: ++;"));
    ASSERT_TRUE(Parse("ns:: ++;;"));
    ASSERT_TRUE(Parse("ns::ns:: ++;"));
    ASSERT_TRUE(Parse("ns::ns:: ++;;"));
    ASSERT_TRUE(Parse(":: ++;"));
    ASSERT_TRUE(Parse(":: ++;;"));
    ASSERT_TRUE(Parse("::ns ++;"));
    ASSERT_TRUE(Parse("::ns ++;;"));
    ASSERT_TRUE(Parse("::ns:: ++;"));
    ASSERT_TRUE(Parse("::ns:: ++;;"));
    ASSERT_TRUE(Parse("++_++;"));
    ASSERT_TRUE(Parse("++_++;;"));
    ASSERT_TRUE(Parse("++  eval ++;"));
    ASSERT_TRUE(Parse("++eval++;;"));
    ASSERT_TRUE(Parse("++eval()++;"));
    ASSERT_TRUE(Parse("++  eval() ++;;"));
    ASSERT_TRUE(Parse("++0++;"));
    ASSERT_TRUE(Parse("++0++;;"));
    ASSERT_TRUE(Parse("++  0.1 ++;"));
    ASSERT_TRUE(Parse("++  0.1    ++;;"));
    ASSERT_TRUE(Parse("++[0,]++;"));
    ASSERT_TRUE(Parse("++(0,)++;;"));
    ASSERT_TRUE(Parse("++(0,1,2,3,):Class++;;"));
    ASSERT_TRUE(Parse("++(0,2,3,) :Class ++;;"));
    ASSERT_TRUE(Parse("++(0,)++;;"));
    ASSERT_TRUE(Parse("++:Class(var)++;"));
    ASSERT_TRUE(Parse("++:Class[0](1)++;"));
    ASSERT_TRUE(Parse("++call()++;"));
    ASSERT_TRUE(Parse("++call()++;;"));
    ASSERT_TRUE(Parse("++call(arg)++;"));
    ASSERT_TRUE(Parse("++call(arg)++;;"));
    ASSERT_TRUE(Parse("++:class++;"));
    ASSERT_TRUE(Parse("++:class++;;"));
    ASSERT_TRUE(Parse("++:class()++;"));
    ASSERT_TRUE(Parse("++:class()++;;"));
    ASSERT_TRUE(Parse("++:class(arg)++;"));
    ASSERT_TRUE(Parse("++:class(arg)++;;"));
    ASSERT_TRUE(Parse("ns:: ++:class++;"));
    ASSERT_TRUE(Parse("ns:: ++:class++;;"));
    ASSERT_TRUE(Parse("ns::ns:: ++:class++;"));
    ASSERT_TRUE(Parse("ns::ns:: ++:class++;;"));
    ASSERT_TRUE(Parse(":: ++:class++;"));
    ASSERT_TRUE(Parse(":: ++:class++;;"));
    ASSERT_TRUE(Parse("::ns ++:class++;"));
    ASSERT_TRUE(Parse("::ns ++:class++;;"));
    ASSERT_TRUE(Parse("::ns:: ++:class++;"));
    ASSERT_TRUE(Parse("::ns:: ++:class++;;"));

    ASSERT_TRUE(Parse("+-;"));
    ASSERT_TRUE(Parse("+-;;"));
    ASSERT_TRUE(Parse("ns:: +-;"));
    ASSERT_TRUE(Parse("ns:: +-;;"));
    ASSERT_TRUE(Parse("ns::ns:: +-;"));
    ASSERT_TRUE(Parse("ns::ns:: +-;;"));
    ASSERT_TRUE(Parse(":: +-;"));
    ASSERT_TRUE(Parse(":: +-;;"));
    ASSERT_TRUE(Parse("::ns +-;"));
    ASSERT_TRUE(Parse("::ns +-;;"));
    ASSERT_TRUE(Parse("::ns:: +-;"));
    ASSERT_TRUE(Parse("::ns:: +-;;"));
    ASSERT_TRUE(Parse("ns:: +-:class+-;"));
    ASSERT_TRUE(Parse("ns:: +-:class+-;;"));
    ASSERT_TRUE(Parse("ns::ns:: +-:class+-;"));
    ASSERT_TRUE(Parse("ns::ns:: +-:class+-;;"));
    ASSERT_TRUE(Parse(":: +-:class+-;"));
    ASSERT_TRUE(Parse(":: +-:class+-;;"));
    ASSERT_TRUE(Parse("::ns +-:class+-;"));
    ASSERT_TRUE(Parse("::ns +-:class+-;;"));
    ASSERT_TRUE(Parse("::ns:: +-:class+-;"));
    ASSERT_TRUE(Parse("::ns:: +-:class+-;;"));

    ASSERT_TRUE(Parse("-+;"));
    ASSERT_TRUE(Parse("-+;;"));
    ASSERT_TRUE(Parse("ns:: -+;"));
    ASSERT_TRUE(Parse("ns:: -+;;"));
    ASSERT_TRUE(Parse("ns::ns:: -+;"));
    ASSERT_TRUE(Parse("ns::ns:: -+;;"));
    ASSERT_TRUE(Parse(":: -+;"));
    ASSERT_TRUE(Parse(":: -+;;"));
    ASSERT_TRUE(Parse("::ns -+;"));
    ASSERT_TRUE(Parse("::ns -+;;"));
    ASSERT_TRUE(Parse("::ns:: -+;"));
    ASSERT_TRUE(Parse("::ns:: -+;;"));
    ASSERT_TRUE(Parse("ns:: -+:class-+;"));
    ASSERT_TRUE(Parse("ns:: -+:class-+;;"));
    ASSERT_TRUE(Parse("ns::ns:: -+:class-+;"));
    ASSERT_TRUE(Parse("ns::ns:: -+:class-+;;"));
    ASSERT_TRUE(Parse(":: -+:class-+;"));
    ASSERT_TRUE(Parse(":: -+:class-+;;"));
    ASSERT_TRUE(Parse("::ns -+:class-+;"));
    ASSERT_TRUE(Parse("::ns -+:class-+;;"));
    ASSERT_TRUE(Parse("::ns:: -+:class-+;"));
    ASSERT_TRUE(Parse("::ns:: -+:class-+;;"));
}

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
    ASSERT_NO_THROW(term = Parse("@@ name @@ := macro", macro));
    ASSERT_TRUE(term);
    ASSERT_TRUE(term->m_left) << term->toString();
    ASSERT_TRUE(term->m_right) << term->toString();

    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("{ @@ name @@ := macro }", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    m_ctx.diag().clear();
    EXPECT_NO_THROW(Parse("{+ @@ name @@ := macro +}", macro));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
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

// -- Документирующие комментарии (TermID::DOCUMENT) --

namespace {
// Рекурсивный подсчёт DOCUMENT-термов в дереве Term.
int countDocTerms(const trust::TermPtr& t) {
    if (!t) {
        return 0;
    }
    int n = (t->getTermID() == TermID::DOCUMENT) ? 1 : 0;
    for (const auto& c : t->m_sequence) {
        n += countDocTerms(c);
    }
    if (t->m_left) {
        n += countDocTerms(t->m_left);
    }
    if (t->m_right) {
        n += countDocTerms(t->m_right);
    }
    if (t->m_args) {
        for (const auto& [name, v] : *t->m_args) {
            (void)name, n += countDocTerms(v);
        }
    }
    return n;
}

// Рекурсивный подсчёт доков, привязанных грамматикой к термам-идентификаторам (m_docs).
int countDeclDocs(const trust::TermPtr& t) {
    if (!t) {
        return 0;
    }
    int n = static_cast<int>(t->m_docs.size());
    for (const auto& c : t->m_sequence) {
        n += countDeclDocs(c);
    }
    if (t->m_left) {
        n += countDeclDocs(t->m_left);
    }
    if (t->m_right) {
        n += countDeclDocs(t->m_right);
    }
    if (t->m_args) {
        for (const auto& [name, v] : *t->m_args) {
            (void)name, n += countDeclDocs(v);
        }
    }
    return n;
}
} // namespace

TEST_F(ParserTest, DocCommentAnywhereInSequence) {
    // Док перед объявлением, standalone док, док внутри блока функции.
    ASSERT_TRUE(Parse("/// перед переменной\nx := 42;\n/// после\n%f():Void := {\n/// внутри функции\n++ 1 ++;\n};"));
    // Доки, привязанные к объявлениям (`x`, `%f`), лежат в m_docs терма-идентификатора и
    // НЕ являются отдельными DOCUMENT-термами. Док перед не-объявлением (`/// внутри функции`
    // перед `++ 1 ++;`) остаётся sibling-узлом DOCUMENT. Без ошибок синтаксиса.
    ASSERT_EQ(countDeclDocs(ast), 2) << "x and %f must carry their leading docs in m_docs";
    ASSERT_EQ(countDocTerms(ast), 1) << "only the non-decl doc remains a DOCUMENT term";
}

TEST_F(ParserTest, DocCommentFullTextInTerm) {
    ASSERT_TRUE(Parse("/// док\nx := 42;"));
    // Док привязан к терму-идентификатору объявления (m_docs) и сохраняется целиком с маркером '///'.
    bool found = false;
    std::function<void(const trust::TermPtr&)> visit = [&](const trust::TermPtr& t) {
        if (!t || found) {
            return;
        }
        for (const auto& d : t->m_docs) {
            EXPECT_EQ("/// док", std::string(d->getText()));
            found = true;
            return;
        }
        for (const auto& c : t->m_sequence) {
            visit(c);
        }
        if (t->m_left) {
            visit(t->m_left);
        }
        if (t->m_right) {
            visit(t->m_right);
        }
        if (t->m_args) {
            for (const auto& [name, v] : *t->m_args) {
                (void)name, visit(v);
            }
        }
    };
    visit(ast);
    ASSERT_TRUE(found) << "doc comment must be attached to the declaration term (m_docs)";
}
