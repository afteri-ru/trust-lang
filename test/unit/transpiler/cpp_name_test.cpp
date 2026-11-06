// test/unit/transpiler/cpp_name_test.cpp
// Единая точка C++-имени (`cppName`, emit_common.hpp): типы, функции, значения/статические
// члены, поля, операторы, нативные символы, entry. Квалифицированное имя разбирается здесь;
// `nameToCpp` получает только один сегмент.
#include "transpiler/emit/emit_common.hpp"
#include "gtest/gtest.h"

namespace trust {
namespace {

TEST(CppNameTest, QualifiedTypeFunctionAndValue) {
    EXPECT_EQ(cppName("Point", CppEntityKind::kType), "Point");
    EXPECT_EQ(cppName("ns::Point", CppEntityKind::kType), "ns::Point");
    EXPECT_EQ(cppName("ns::sub::Point", CppEntityKind::kType), "ns::sub::Point");
    EXPECT_EQ(cppName("ns::f", CppEntityKind::kFunction), "ns::f");
    EXPECT_EQ(cppName("ns::x", CppEntityKind::kValue), "ns::x");
    EXPECT_EQ(cppName("ns::Class::field", CppEntityKind::kValue), "ns::Class::field");
    EXPECT_EQ(cppName("::f", CppEntityKind::kFunction), "f");
}

TEST(CppNameTest, FieldNeverQualified) {
    EXPECT_EQ(cppName("field", CppEntityKind::kField), "field");
}

TEST(CppNameTest, OperatorAndEntry) {
    EXPECT_EQ(cppName("__eq__", CppEntityKind::kOperator), "operator==");
    EXPECT_EQ(cppName("mod__main__", CppEntityKind::kEntry), "mod__main__");
}

} // namespace
} // namespace trust
