#include "syntax/parser_test_fixture.hpp"
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

// Позиция синтаксической ошибки при пропущенном ';' после ':Enum' должна указывать на
// конец последнего корректного токена (место пропущенного разделителя), а не на
// следующую декларацию. Раньше tokenStartOffset()/flex-курсор при упреждающей
// буферизации указывал в произвольное место (конец/середину следующего оператора).
TEST_F(ParserTest, SyntaxErrorPointsAtMissingSemicolon) {
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse(R"(Level ::= (LOW='low', HIGH='high',):Enum
Data ::= (i=42,):Variant;)"));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);

    const DiagnosticEntry* syntax = nullptr;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (d.message.find("syntax error") != std::string::npos) {
            syntax = &d;
            break;
        }
    }
    ASSERT_NE(syntax, nullptr) << "must report a syntax error";

    // Пропущенный ';' - конец строки "Level ::= (LOW='low', HIGH='high',):Enum".
    const auto begin = m_ctx.source().line_column(syntax->range.begin);
    const auto end = m_ctx.source().line_column(syntax->range.end);
    EXPECT_EQ(1u, begin.line) << "error must point at the line of the missing ';', not the next decl";
    EXPECT_GE(begin.column, 37u);
    EXPECT_TRUE(syntax->range.is_point()) << "multi-line gap must collapse to a point at the missing ';'";
    EXPECT_EQ(begin.line, end.line);
}

TEST_F(ParserTest, EnumVariantWithSemicolonValid) {
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse(R"(Level ::= (LOW='low', HIGH='high',):Enum;
Data ::= (i=42,):Variant;)"));
    EXPECT_EQ(0, m_ctx.diag().errorCount()) << "with ';' after :Enum the file is valid";
}

TEST_F(ParserTest, EnumAtEofWithoutSemicolonValid) {
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("Level ::= (LOW='low', HIGH='high',):Enum"));
    EXPECT_EQ(0, m_ctx.diag().errorCount()) << "single :Enum decl at EOF without ';' is valid";
}

// Однострочный разрыв (пропущенный ',' в кортеже) -> диапазон-промежуток, а не точка:
// подчёркивается именно промежуток [конец последнего токена, начало неожиданного].
TEST_F(ParserTest, SyntaxErrorMissingCommaSameLineIsGap) {
    m_ctx.diag().clear();
    ASSERT_NO_THROW(Parse("Level ::= (LOW='low' HIGH='high',):Enum;"));
    ASSERT_GT(m_ctx.diag().errorCount(), 0);

    const DiagnosticEntry* syntax = nullptr;
    for (const auto& d : m_ctx.diag().diagnostics()) {
        if (d.message.find("syntax error") != std::string::npos) {
            syntax = &d;
            break;
        }
    }
    ASSERT_NE(syntax, nullptr) << "must report a syntax error";
    EXPECT_EQ(1u, m_ctx.source().line_column(syntax->range.begin).line);
    EXPECT_FALSE(syntax->range.is_point()) << "same-line gap must be a range, not a point";
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
