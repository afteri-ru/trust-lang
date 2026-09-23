// Test file: trust::AnyIterator - type-erased universal iterator (single-pass cursor).
#include "stdlib/any_iterator.hpp"

#include <cstdint>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

using trust::AnyIterator;
using trust::makeAnyIterator;

namespace {

TEST(AnyIteratorTest, FromNativeIterators) {
    std::vector<int> values{1, 2, 3, 4};
    AnyIterator<int> it{values.begin(), values.end()};
    int sum = 0;
    while (it.hasNext()) {
        sum += it.next();
    }
    EXPECT_EQ(sum, 10);
    EXPECT_FALSE(it.hasNext());
    EXPECT_FALSE(static_cast<bool>(it));
}

TEST(AnyIteratorTest, FromContainer) {
    std::vector<int> values{1, 2, 3};
    AnyIterator<int> it{values};
    EXPECT_EQ(it.next(), 1);
    EXPECT_EQ(it.next(), 2);
    EXPECT_EQ(it.next(), 3);
    EXPECT_FALSE(it.hasNext());
}

TEST(AnyIteratorTest, ErasesConcreteIteratorType) {
    std::vector<int> vector_values{1, 2};
    std::list<int> list_values{3, 4};
    AnyIterator<int> vector_it{vector_values};
    AnyIterator<int> list_it{list_values};
    static_assert(std::is_same_v<decltype(vector_it), decltype(list_it)>);
    EXPECT_EQ(vector_it.next(), 1);
    EXPECT_EQ(list_it.next(), 3);
}

TEST(AnyIteratorTest, DereferenceWithoutAdvance) {
    std::vector<int> values{7, 8};
    AnyIterator<int> it{values};
    EXPECT_EQ(*it, 7);
    EXPECT_EQ(*it, 7);
    ++it;
    EXPECT_EQ(*it, 8);
}

TEST(AnyIteratorTest, DefaultConstructedIsExhausted) {
    AnyIterator<int> it;
    EXPECT_FALSE(it.hasNext());
    EXPECT_THROW(it.next(), std::out_of_range);
    EXPECT_THROW(*it, std::out_of_range);
    EXPECT_THROW(++it, std::out_of_range);
}

TEST(AnyIteratorTest, ExhaustedThrows) {
    std::vector<int> values{1};
    AnyIterator<int> it{values};
    EXPECT_EQ(it.next(), 1);
    EXPECT_THROW(it.next(), std::out_of_range);
}

TEST(AnyIteratorTest, IsSinglePassMoveOnly) {
    static_assert(!std::is_copy_constructible_v<AnyIterator<int>>);
    static_assert(!std::is_copy_assignable_v<AnyIterator<int>>);
    static_assert(std::is_move_constructible_v<AnyIterator<int>>);
    static_assert(std::is_move_assignable_v<AnyIterator<int>>);

    std::vector<int> values{1, 2};
    AnyIterator<int> source{values};
    AnyIterator<int> moved{std::move(source)};
    EXPECT_EQ(moved.next(), 1);
}

TEST(AnyIteratorTest, RangeForWithDefaultSentinel) {
    std::vector<int> values{1, 2, 3};
    AnyIterator<int> it{values};
    int sum = 0;
    for (; it != std::default_sentinel; ++it) {
        sum += *it;
    }
    EXPECT_EQ(sum, 6);
}

TEST(AnyIteratorTest, MapValues) {
    std::map<std::string, int> values{{"a", 1}, {"b", 2}};
    AnyIterator<std::pair<const std::string, int>> it{values};
    int sum = 0;
    while (it.hasNext()) {
        sum += it.next().second;
    }
    EXPECT_EQ(sum, 3);
}

TEST(AnyIteratorTest, Factories) {
    std::vector<int> values{1, 2, 3};
    AnyIterator<int> from_iterators = makeAnyIterator<int>(values.begin(), values.end());
    AnyIterator<int> from_range = makeAnyIterator<int>(values);
    AnyIterator<int> deduced = makeAnyIterator(values); // Value выведен из элемента диапазона
    static_assert(std::is_same_v<decltype(deduced), AnyIterator<int>>);
    EXPECT_EQ(from_iterators.next(), 1);
    EXPECT_EQ(from_range.next(), 1);
    EXPECT_EQ(deduced.next(), 1);
}

} // namespace
