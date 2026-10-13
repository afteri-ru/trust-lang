// test/unit/transpiler/cpp_name_test.cpp
// Единая точка C++-имени (`cppName`, emit_common.hpp): типы, функции, значения/статические
// члены, поля, операторы, нативные символы, entry. Квалифицированное имя разбирается здесь;
// `nameToCpp` получает только один сегмент.
#include "transpiler/emit/emit_common.hpp"
#include "gtest/gtest.h"

namespace trust {
namespace {

TEST(CppNameTest, QualifiedTypeFunctionAndValue) {
    EXPECT_EQ(cppName("Point", CppEntityKind::kType), "c_Point");
    EXPECT_EQ(cppName("ns::Point", CppEntityKind::kType), "c_ns::c_Point");
    EXPECT_EQ(cppName("ns::sub::Point", CppEntityKind::kType), "c_ns::c_sub::c_Point");
    EXPECT_EQ(cppName("ns::f", CppEntityKind::kFunction), "c_ns::c_f");
    EXPECT_EQ(cppName("ns::x", CppEntityKind::kValue), "c_ns::c_x");
    EXPECT_EQ(cppName("ns::Class::field", CppEntityKind::kValue), "c_ns::c_Class::c_field");
    EXPECT_EQ(cppName("::f", CppEntityKind::kFunction), "c_f");
}

TEST(CppNameTest, FieldNeverQualified) {
    EXPECT_EQ(cppName("field", CppEntityKind::kField), "c_field");
}

TEST(CppNameTest, OperatorAndNativeAndEntry) {
    EXPECT_EQ(cppName("__eq__", CppEntityKind::kOperator), "operator==");
    EXPECT_EQ(cppName("%std::max", CppEntityKind::kFunction), "std::max");
    EXPECT_EQ(cppName("%lib::f", CppEntityKind::kFunction), "lib::f");
    EXPECT_EQ(cppName("mod__main__", CppEntityKind::kEntry), "mod__main__");
}

} // namespace
} // namespace trust
