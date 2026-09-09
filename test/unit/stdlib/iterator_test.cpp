// Test file: trust::IteratorRange - universal iterator-cursor (bundled range).
#include "stdlib/iterator.hpp"

#include <cstdint>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

using trust::IteratorRange;
using trust::makeIteratorRange;

namespace {

TEST(IteratorRangeTest, FromNativeIterators) {
    std::vector<int> values{1, 2, 3, 4};
    IteratorRange it{values.begin(), values.end()};
    int sum = 0;
    while (it) {
        sum += *it;
        ++it;
    }
    EXPECT_EQ(sum, 10);
    EXPECT_FALSE(it);
    EXPECT_TRUE(it.empty());
}

TEST(IteratorRangeTest, CursorAdvancesAndCompares) {
    std::vector<int> values{10, 20};
    IteratorRange it{values.begin(), values.end()};
    EXPECT_EQ(*it, 10);
    EXPECT_EQ(it.current(), values.begin());
    EXPECT_EQ(it.limit(), values.end());
    IteratorRange copy = it++;
    EXPECT_EQ(*copy, 10);
    EXPECT_EQ(*it, 20);
    ++it;
    EXPECT_TRUE(it.empty());
    EXPECT_EQ(it.current(), values.end());
}

TEST(IteratorRangeTest, FromContainerIsBorrowedView) {
    std::vector<int> values{1, 2, 3, 4};
    IteratorRange it{values};
    EXPECT_EQ(it.size(), 4);
    const auto& limit = it.limit();
    EXPECT_EQ(limit, values.end());
}

TEST(IteratorRangeTest, DefaultConstructedIsEmpty) {
    IteratorRange<std::vector<int>::iterator> it;
    EXPECT_TRUE(it.empty());
    EXPECT_FALSE(static_cast<bool>(it));
}

TEST(IteratorRangeTest, DereferenceExhaustedThrows) {
    std::vector<int> values{1};
    IteratorRange it{values.begin(), values.end()};
    ++it;
    EXPECT_TRUE(it.empty());
    EXPECT_THROW(*it, std::out_of_range);
}

TEST(IteratorRangeTest, RandomAccessOperations) {
    std::vector<int> values{1, 2, 3, 4};
    IteratorRange it{values.begin(), values.end()};
    it += 2;
    EXPECT_EQ(*it, 3);
    EXPECT_EQ(it.size(), 2);
    it -= 1;
    EXPECT_EQ(*it, 2);
}

TEST(IteratorRangeTest, BidirectionalOperations) {
    std::list<int> values{1, 2, 3};
    IteratorRange it{values.begin(), values.end()};
    ++it;
    ++it;
    EXPECT_EQ(*it, 3);
    --it;
    EXPECT_EQ(*it, 2);
    IteratorRange copy = it--;
    EXPECT_EQ(*copy, 2);
    EXPECT_EQ(*it, 1);
}

TEST(IteratorRangeTest, MutableWriteThrough) {
    std::vector<int> values{1, 2, 3};
    IteratorRange it{values.begin(), values.end()};
    *it = 100;
    EXPECT_EQ(values[0], 100);
}

TEST(IteratorRangeTest, ConstIteratorYieldsConstReference) {
    std::vector<int> values{1, 2, 3};
    IteratorRange it{values.cbegin(), values.cend()};
    static_assert(std::is_same_v<decltype(*it), const int&>);
    EXPECT_EQ(*it, 1);
}

TEST(IteratorRangeTest, RawPointerIterators) {
    int raw[] = {5, 6, 7};
    IteratorRange it{raw, raw + 3};
    int sum = 0;
    for (int value : it) {
        sum += value;
    }
    EXPECT_EQ(sum, 18);
}

TEST(IteratorRangeTest, RangeForIteratesRemaining) {
    std::vector<int> values{1, 2, 3, 4};
    IteratorRange it{values.begin(), values.end()};
    ++it;
    int sum = 0;
    for (int value : it) {
        sum += value;
    }
    EXPECT_EQ(sum, 9); // 2 + 3 + 4
}

TEST(IteratorRangeTest, MapKeyValuePair) {
    std::map<std::string, int> values{{"a", 1}, {"b", 2}};
    IteratorRange it{values};
    int sum = 0;
    for (const auto& entry : it) {
        sum += entry.second;
    }
    EXPECT_EQ(sum, 3);
}

TEST(IteratorRangeTest, ResetAndEquality) {
    std::vector<int> values{1, 2, 3};
    IteratorRange lhs{values.begin(), values.end()};
    IteratorRange rhs{values.begin(), values.end()};
    EXPECT_TRUE(lhs == rhs);
    ++rhs;
    EXPECT_FALSE(lhs == rhs);
    lhs.reset(values.begin(), values.end());
    EXPECT_EQ(*lhs, 1);
}

TEST(IteratorRangeTest, Factories) {
    std::vector<int> values{1, 2, 3};
    auto from_iterators = makeIteratorRange(values.begin(), values.end());
    auto from_range = makeIteratorRange(values);
    static_assert(std::is_same_v<decltype(from_iterators), decltype(from_range)>);
    EXPECT_EQ(from_iterators.size(), 3);
    EXPECT_EQ(from_range.size(), 3);
    int sum = 0;
    for (int value : from_range) {
        sum += value;
    }
    EXPECT_EQ(sum, 6);
}

} // namespace
