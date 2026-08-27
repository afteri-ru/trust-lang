#include "syntax/parser_test_fixture.hpp"
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
//     ASSERT_TRUE(Parse("# @@macro @@@@"));
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
