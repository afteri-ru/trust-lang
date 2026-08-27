// Test file: Dict runtime type (universal heterogeneous dictionary with TypedValue).
#include "trust/dict.hpp"
#include <cstdint>
#include <format>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <variant>

using trust::Dict;
using trust::Rational;
using trust::TypedValue;

namespace {

// TypeKind: Group(0-7) | Data/размерность(8-15). Значения групп - кодировка (ABI)
// trust::TypedValue; здесь собраны виды, используемые в тестах словаря.
constexpr uint32_t kInt32 = 3u | (32u << 8);
constexpr uint32_t kFloat64 = 5u | (64u << 8);
constexpr uint32_t kBool = 2u | (1u << 8);
constexpr uint32_t kStrChar = 9u | (1u << 8);
constexpr uint32_t kDict = 11u | (1u << 8);
constexpr uint32_t kRational = 8u | (1u << 8);

TEST(DictTest, TypedValueKindDecode) {
    // Декодирование TypeKind методами TypedValue (группа/размерность/предикаты).
    const TypedValue tvInt8{3u | (8u << 8), int8_t(1)};
    EXPECT_EQ(tvInt8.group(), 3u);
    EXPECT_EQ(tvInt8.data(), 8u);
    EXPECT_TRUE(tvInt8.isInteger());
    EXPECT_TRUE(tvInt8.isNumeric());
    EXPECT_FALSE(tvInt8.isString());

    const TypedValue tvBool{2u | (1u << 8), true};
    EXPECT_TRUE(tvBool.isBool());
    EXPECT_FALSE(tvBool.isNumeric()); // Bool - логическая группа, не арифметическая

    const TypedValue tvStr{9u | (1u << 8), std::string("x")};
    EXPECT_TRUE(tvStr.isStrChar());
    EXPECT_TRUE(tvStr.isString());

    const TypedValue tvF{5u | (64u << 8), 1.0};
    EXPECT_TRUE(tvF.isFloat());
    EXPECT_TRUE(tvF.isNumeric());

    const TypedValue tvDict{11u | (1u << 8), Dict{}};
    EXPECT_TRUE(tvDict.isDict());
}

TEST(DictTest, DefaultCtorEmpty) {
    Dict d;
    EXPECT_TRUE(d.empty());
    EXPECT_EQ(d.size(), 0u);
}

TEST(DictTest, InitializerListConstruction) {
    // (1, two=2, name=3,) - безымянный элемент + именованные.
    Dict d{{"", TypedValue{kInt32, 1}}, {"two", TypedValue{kInt32, 2}}, {"name", TypedValue{kInt32, 3}}};
    EXPECT_EQ(d.size(), 3u);
    EXPECT_FALSE(d.empty());
    EXPECT_TRUE(d.contains("two"));
    EXPECT_TRUE(d.contains("name"));
    EXPECT_EQ(d.index("two"), 1);
    EXPECT_EQ(d.index("missing"), -1);
}

TEST(DictTest, AccessByName) {
    Dict d{{"two", TypedValue{kInt32, 2}}, {"name", TypedValue{kInt32, 3}}};
    EXPECT_EQ(d.at("two").getAs<int>(), 2);
    EXPECT_EQ(d["name"].getAs<int>(), 3);
}

TEST(DictTest, AccessByIndexAndNegativeIndex) {
    Dict d{{"", TypedValue{kInt32, 10}}, {"", TypedValue{kInt32, 20}}, {"", TypedValue{kInt32, 30}}};
    EXPECT_EQ(d.at(0).getAs<int>(), 10);
    EXPECT_EQ(d.at(2).getAs<int>(), 30);
    // Отрицательный индекс - с конца.
    EXPECT_EQ(d.at(-1).getAs<int>(), 30);
    EXPECT_EQ(d.at(-3).getAs<int>(), 10);
}

TEST(DictTest, ThrowsOnBadIndex) {
    Dict d{{"", TypedValue{kInt32, 1}}};
    EXPECT_THROW(d.at(1), std::out_of_range);
    EXPECT_THROW(d.at(-2), std::out_of_range);
}

TEST(DictTest, ThrowsOnMissingName) {
    Dict d{{"two", TypedValue{kInt32, 2}}};
    EXPECT_THROW(d.at("nope"), std::out_of_range);
}

TEST(DictTest, PushBackAndClear) {
    Dict d;
    d.push_back("a", TypedValue{kInt32, 1});
    d.push_back("", TypedValue{kInt32, 2});
    EXPECT_EQ(d.size(), 2u);
    EXPECT_TRUE(d.contains("a"));
    d.clear();
    EXPECT_TRUE(d.empty());
}

TEST(DictTest, GetAsTyped) {
    Dict d{{"int", TypedValue{kInt32, 42}},
           {"dbl", TypedValue{kFloat64, 2.5}},
           {"str", TypedValue{kStrChar, std::string("hi")}},
           {"flag", TypedValue{kBool, true}}};
    EXPECT_EQ(d.getAs<int>("int"), 42);
    EXPECT_DOUBLE_EQ(d.getAs<double>("dbl"), 2.5);
    EXPECT_EQ(d.getAs<std::string>("str"), "hi");
    EXPECT_EQ(d.getAs<bool>("flag"), true);
}

TEST(DictTest, GetAsThrowsOnTypeMismatch) {
    Dict d{{"int", TypedValue{kInt32, 42}}};
    EXPECT_THROW(d.getAs<std::string>("int"), std::bad_any_cast);
}

TEST(DictTest, UniversalConverters) {
    Dict d{{"", TypedValue{kInt32, 42}}};
    EXPECT_EQ(d.GetAsInteger(), 42);
    EXPECT_EQ(d.GetAsBoolean(), 1);
    EXPECT_DOUBLE_EQ(d.GetAsNumber(), 42.0);
    Dict s{{"", TypedValue{kStrChar, std::string("7")}}};
    EXPECT_EQ(s.GetAsInteger(), 7);
    EXPECT_EQ(s.GetAsString(), "7");
}

TEST(DictTest, UniversalConvertersOnEmptyThrows) {
    Dict d;
    EXPECT_THROW(d.GetAsInteger(), std::runtime_error);
    EXPECT_THROW(d.GetAsString(), std::runtime_error);
}

TEST(DictTest, Equality) {
    Dict a{{"x", TypedValue{kInt32, 1}}};
    Dict b{{"x", TypedValue{kInt32, 1}}};
    Dict c{{"x", TypedValue{kInt32, 2}}};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(DictTest, NestedDict) {
    // Значение - вложенный словарь (гетерогенность); Dict лежит в std::any-ветке TypedValue.
    Dict inner{{"k", TypedValue{kInt32, 5}}};
    Dict outer{{"inner", TypedValue{kDict, inner}}};
    const Dict got = outer.at("inner").getAs<Dict>();
    EXPECT_EQ(got.getAs<int>("k"), 5);
}

TEST(DictTest, RationalInFastBranch) {
    // Rational - быстрая ветка variant ПО ЗНАЧЕНИЮ (не std::any), как числа/bool/строки.
    const TypedValue tv{kRational, Rational("3", "4")};
    EXPECT_TRUE(tv.isRational());
    EXPECT_TRUE(std::holds_alternative<Rational>(tv.storage));
    EXPECT_FALSE(std::holds_alternative<std::any>(tv.storage));
    EXPECT_EQ(tv.getAs<Rational>().GetAsString(), "3\\4");
}

TEST(DictTest, RationalInDictAccess) {
    Dict d{{"half", TypedValue{kRational, Rational("1", "2")}}};
    // Доступ по известному типу - напрямую из быстрой ветки (getAs<Rational>).
    const Rational got = d.at("half").getAs<Rational>();
    EXPECT_EQ(got.GetAsString(), "1\\2");
    EXPECT_TRUE(std::holds_alternative<Rational>(d.at("half").storage));
}

TEST(DictTest, RationalDictCopyAndEqual) {
    // Копирование Dict с Rational - по значению (внутренний deep-copy Rational, без разделения).
    Dict a{{"r", TypedValue{kRational, Rational("1", "2")}}};
    Dict b = a;
    Dict c{{"r", TypedValue{kRational, Rational("3", "4")}}};
    EXPECT_EQ(a.at("r").getAs<Rational>().GetAsString(), "1\\2");
    EXPECT_EQ(b.at("r").getAs<Rational>().GetAsString(), "1\\2");
    EXPECT_TRUE(a == b); // равные значения → равны
    EXPECT_TRUE(a != c); // разные значения → не равны
}

TEST(DictTest, RationalUniversalConverters) {
    Dict d{{"", TypedValue{kRational, Rational("7", "1")}}};
    EXPECT_EQ(d.GetAsInteger(), 7);
    EXPECT_DOUBLE_EQ(d.GetAsNumber(), 7.0);
    EXPECT_EQ(d.GetAsBoolean(), 1);
    EXPECT_EQ(d.GetAsString(), "7\\1");
}

TEST(DictTest, RationalDictPrint) {
    Dict d{{"half", TypedValue{kRational, Rational("1", "2")}}};
    EXPECT_EQ(std::format("{}", d), "(half=1\\2,)");
}

TEST(DictTest, Truthiness) {
    Dict empty;
    EXPECT_FALSE(static_cast<bool>(empty));
    Dict d{{"", TypedValue{kInt32, 5}}};
    EXPECT_TRUE(static_cast<bool>(d));
}

TEST(DictTest, PopFront) {
    Dict d{{"", TypedValue{kInt32, 1}}, {"", TypedValue{kInt32, 2}}, {"", TypedValue{kInt32, 3}}};
    EXPECT_EQ(d.size(), 3u);
    auto first = d.pop_front();
    EXPECT_EQ(d.size(), 2u);
    EXPECT_EQ(std::any_cast<int64_t>(first), 1);
    d.pop_front();
    d.pop_front();
    EXPECT_TRUE(d.empty());
    EXPECT_THROW(d.pop_front(), std::out_of_range);
}

TEST(DictTest, ToAnyNaturalType) {
    const TypedValue tvBool{kBool, true};
    EXPECT_TRUE(std::any_cast<bool>(tvBool.toAny()));
    const TypedValue tvInt{kInt32, int32_t(42)};
    EXPECT_EQ(std::any_cast<int64_t>(tvInt.toAny()), 42);
    const TypedValue tvStr{kStrChar, std::string("abc")};
    EXPECT_EQ(std::any_cast<std::string>(tvStr.toAny()), "abc");
}

} // namespace
