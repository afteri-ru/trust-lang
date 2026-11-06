// Test file: runtime-шаблон trust::Enum (include/runtime/trust/enum.hpp).
// Контракт: сравнение ПО ЗНАЧЕНИЮ (не по позиции), count(), fromName()/fromValue() по таблице
// kMembers (порядок = ordinal), неизвестное имя/значение → default-член.
#include "trust/enum.hpp"
#include <cstdint>
#include <gtest/gtest.h>

namespace {

// Форма ровно та, что эмитит кодогенератор (`struct c_Color : trust::Enum<...>` + static const).
struct Color : trust::Enum<int8_t, 3, Color> {
    using trust::Enum<int8_t, 3, Color>::Enum;
    static const Color RED;
    static const Color GREEN;
    static const Color BLUE;
    static inline const trust::EnumMember<int8_t> kMembers[3] = {{1, "RED"}, {2, "GREEN"}, {3, "BLUE"}};
};
const Color Color::RED{1, 0};
const Color Color::GREEN{2, 1};
const Color Color::BLUE{3, 2};

TEST(EnumTest, CountMatchesMembers) {
    EXPECT_EQ(Color::count(), 3u);
}

TEST(EnumTest, MembersCarryValueAndOrdinal) {
    EXPECT_EQ(Color::GREEN.value, 2);
    EXPECT_EQ(Color::GREEN.ordinal, 1u);
}

TEST(EnumTest, ComparisonUsesValueNotOrdinal) {
    // RED=1, GREEN=2: сравнение по значению -> RED < GREEN, равенство по значению.
    EXPECT_TRUE(Color::RED == Color::RED);
    EXPECT_TRUE(Color::RED != Color::GREEN);
    EXPECT_TRUE(Color::RED < Color::GREEN);
}

TEST(EnumTest, FromNameFindsMember) {
    EXPECT_TRUE(Color::fromName("GREEN") == Color::GREEN);
    EXPECT_EQ(Color::fromName("BLUE").ordinal, 2u);
}

TEST(EnumTest, FromValueFindsMember) {
    EXPECT_TRUE(Color::fromValue(3) == Color::BLUE);
}

TEST(EnumTest, UnknownNameOrValueYieldsDefault) {
    // Таблица не содержит значения 99/имени "NOPE" -> default-конструированный член.
    EXPECT_EQ(Color::fromName("NOPE").ordinal, 0u);
    EXPECT_EQ(Color::fromValue(99).value, 0);
}

} // namespace