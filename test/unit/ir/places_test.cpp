// test/unit/ir/places_test.cpp
// PlaceId/PlaceTable: дедупликация, стабильный порядок, поиск отсутствующего ключа,
// ключи-указатели (HIR-адаптер) и скалярные ключи (MIR-адаптер).

#include "ir/places.hpp"

#include <gtest/gtest.h>

namespace trust::flow {
namespace {

TEST(PlaceTable, AddIsIdempotentAndKeepsInsertionOrder) {
    PlaceTable<int> table;
    const PlaceId first = table.add(10);
    const PlaceId second = table.add(20);
    const PlaceId duplicate = table.add(10);

    EXPECT_EQ(first.value, 0u);
    EXPECT_EQ(second.value, 1u);
    EXPECT_EQ(duplicate.value, first.value);
    EXPECT_EQ(table.size(), 2u);
}

TEST(PlaceTable, LookupReportsMissingKey) {
    PlaceTable<int> table;
    (void)table.add(42);

    const auto found = table.find(42);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->value, 0u);
    EXPECT_FALSE(table.find(7).has_value());
}

TEST(PlaceTable, PointerKeysAreSupported) {
    int x = 0;
    int y = 0;
    PlaceTable<const int*> table;

    const PlaceId px = table.add(&x);
    const PlaceId py = table.add(&y);
    EXPECT_EQ(px.value, 0u);
    EXPECT_EQ(py.value, 1u);
    EXPECT_EQ(table.add(&x).value, px.value);
    const auto found = table.find(&y);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->value, py.value);
}

} // namespace
} // namespace trust::flow
