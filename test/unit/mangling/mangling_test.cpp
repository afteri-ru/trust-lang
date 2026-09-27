// test/unit/mangling/mangling_test.cpp - единые правила именования компонента `mangling`.
#include "mangling/mangling.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace trust;

TEST(Mangling, SanitizeSymbolPartIsInjective) {
    EXPECT_EQ(mangling::sanitizeSymbolPart("abcXYZ019"), "abcXYZ019");
    EXPECT_EQ(mangling::sanitizeSymbolPart("ns::operator=="), "ns_3a_3aoperator_3d_3d");
    EXPECT_EQ(mangling::sanitizeSymbolPart("_"), "_5f");
    EXPECT_EQ(mangling::sanitizeSymbolPart(""), "");
    // Инъективность: '_' кодируется, поэтому 'a_' и 'aA' дают разные символы.
    EXPECT_NE(mangling::sanitizeSymbolPart("a_"), mangling::sanitizeSymbolPart("aA"));
}

TEST(Mangling, CppFuncIdentRules) {
    // Обычное имя: name_to_cpp добавляет префикс c_.
    EXPECT_EQ(mangling::cppFuncIdent("foo", false, false), "c_foo");
    // Нативное имя (%name): '%' срезается (совпадает с C-символом).
    EXPECT_EQ(mangling::cppFuncIdent("%abs", false, false), "abs");
    // Локальное имя ($x): '$' срезается.
    EXPECT_EQ(mangling::cppFuncIdent("$x", false, false), "c_x");
    // entry-функция: сырое имя без манглинга.
    EXPECT_EQ(mangling::cppFuncIdent("mod__main__", false, true), "mod__main__");
    // Оператор: operator<sym>.
    EXPECT_EQ(mangling::cppFuncIdent("==", true, false), "operator==");
    EXPECT_EQ(mangling::cppFuncIdent("()", true, false), "operator()");
}

TEST(Mangling, ExportSymbolSchemes) {
    EXPECT_EQ(mangling::exportSymbol("c_foo", mangling::ExportKind::Function), "__trust_fn_c_5ffoo");
    EXPECT_EQ(mangling::exportSymbol("c_x", mangling::ExportKind::Variable), "__trust_var_c_5fx");
    EXPECT_EQ(mangling::exportSymbol("operator==", mangling::ExportKind::Operator), "__trust_op_operator_3d_3d");
}

TEST(Mangling, InterfaceDeclBinding) {
    EXPECT_EQ(mangling::interfaceDecl("x:Int32 := ...;", "%__trust_var_c_5fx"), "x:Int32 := %__trust_var_c_5fx;");
    EXPECT_EQ(mangling::interfaceDecl("`==`(a:V, b:V):Bool := ...;", "%__trust_op_operator_3d_3d"), "`==`(a:V, b:V):Bool := %__trust_op_operator_3d_3d;");
}

TEST(Mangling, TmpNameSchemes) {
    EXPECT_EQ(mangling::tmpName(mangling::TempKind::Match, 1), "_match1");
    EXPECT_EQ(mangling::tmpName(mangling::TempKind::Result, 2), "__trust_res_2");
    EXPECT_EQ(mangling::tmpName(mangling::TempKind::Discard, 3), "__trust_discard_3");
}
