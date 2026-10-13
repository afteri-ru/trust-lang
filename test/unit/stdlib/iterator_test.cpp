// Test file: trust::Iterator - the single universal type-erased iterator.
#include "stdlib/core/iterator.hpp"

#include <concepts>
#include <cstdint>
#include <forward_list>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

using trust::Iterator;
using trust::makeIterator;

namespace {

TEST(IteratorTest, FromNativeIterators) {
    std::vector<int> values{1, 2, 3, 4};
    Iterator<int> it{values.begin(), values.end()};
    int sum = 0;
    while (it) {
        sum += *it;
        ++it;
    }
    EXPECT_EQ(sum, 10);
    EXPECT_FALSE(static_cast<bool>(it));
}

TEST(IteratorTest, FromContainer) {
    std::vector<int> values{1, 2, 3};
    Iterator<int> it{values};
    EXPECT_EQ(*it, 1);
    EXPECT_EQ(*it++, 1);
    EXPECT_EQ(*it, 2);
    EXPECT_EQ(*++it, 3);
    EXPECT_TRUE(it.hasNext());
    ++it;
    EXPECT_FALSE(it.hasNext());
}

TEST(IteratorTest, CopyIsIndependent) {
    std::vector<int> values{1, 2, 3};
    Iterator<int> it{values};
    Iterator<int> copy = it;
    ++copy;
    EXPECT_EQ(*it, 1);
    EXPECT_EQ(*copy, 2);
}

TEST(IteratorTest, ReverseWithExplicitIterators) {
    std::vector<int> values{1, 2, 3};
    Iterator<int> it{values.rbegin(), values.rend()};
    int sum = 0;
    while (it) {
        sum += *it;
        ++it;
    }
    EXPECT_EQ(sum, 6);
}

TEST(IteratorTest, DecrementBidirectional) {
    std::list<int> values{1, 2, 3};
    Iterator<int> it{values.begin(), values.end()};
    ++it;
    ++it;
    EXPECT_EQ(*it, 3);
    EXPECT_EQ(*it--, 3);
    EXPECT_EQ(*it, 2);
    EXPECT_EQ(*--it, 1);
}

TEST(IteratorTest, DecrementForwardOnlyThrows) {
    // input_iterator без bidirectional: std::istream_iterator-like поведение моделируем
    // через forward-итератор-подобный тип нельзя; проверяем на std::forward_list.
    std::forward_list<int> values{1, 2};
    Iterator<int> it{values.begin(), values.end()};
    EXPECT_THROW(--it, std::logic_error);
}

TEST(IteratorTest, EmptyIteratorThrows) {
    Iterator<int> it;
    EXPECT_FALSE(it.hasNext());
    EXPECT_THROW(*it, std::out_of_range);
    EXPECT_THROW(++it, std::out_of_range);
    EXPECT_THROW(--it, std::out_of_range);
}

TEST(IteratorTest, ExhaustedThrows) {
    std::vector<int> values{1};
    Iterator<int> it{values};
    EXPECT_EQ(*it, 1);
    ++it;
    EXPECT_FALSE(it.hasNext());
    EXPECT_THROW(*it, std::out_of_range);
    EXPECT_THROW(++it, std::out_of_range);
}

TEST(IteratorTest, MapValues) {
    std::map<std::string, int> values{{"a", 1}, {"b", 2}};
    Iterator<std::pair<const std::string, int>> it{values};
    int sum = 0;
    while (it) {
        sum += (*it).second;
        ++it;
    }
    EXPECT_EQ(sum, 3);
}

TEST(IteratorTest, Factories) {
    std::vector<int> values{1, 2, 3};
    auto from_iterators = makeIterator<int>(values.begin(), values.end());
    auto from_range = makeIterator<int>(values);
    static_assert(std::is_same_v<decltype(from_iterators), decltype(from_range)>);
    EXPECT_EQ(*from_iterators, 1);
    EXPECT_EQ(*from_range, 1);
}

TEST(IteratorTest, RejectsTemporaryContainer) {
    static_assert(!std::constructible_from<Iterator<int>, std::vector<int>>);
    static_assert(std::constructible_from<Iterator<int>, std::vector<int>&>);
}

} // namespace
