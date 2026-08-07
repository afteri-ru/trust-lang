// Test file: Range runtime type (universal arithmetic range, inclusive semantics).
#include "trust/range.hpp"

#include <any>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using trust::Dict;
using trust::Range;
using trust::Rational;

namespace {

// ── Целые ──────────────────────────────────────────────────────────────────

TEST(RangeTest, DefaultStepAscending) {
    const Range<std::int64_t> r(1, 10);
    EXPECT_EQ(r.start(), 1);
    EXPECT_EQ(r.stop(), 10);
    EXPECT_EQ(r.step(), 1);    // start <= stop → +1
    EXPECT_EQ(r.count(), 10u); // 1..10 (инклюзивно)
    EXPECT_FALSE(r.empty());
    EXPECT_EQ(r.at(0), 1);
    EXPECT_EQ(r.at(9), 10);
    EXPECT_THROW(r.at(10), std::out_of_range);
}

TEST(RangeTest, DefaultStepDescending) {
    const Range<std::int64_t> r(10, 1);
    EXPECT_EQ(r.step(), -1);   // start > stop → -1
    EXPECT_EQ(r.count(), 10u); // 10..1
    EXPECT_EQ(r.at(0), 10);
    EXPECT_EQ(r.at(9), 1);
}

TEST(RangeTest, ExplicitStep) {
    const Range<std::int64_t> r(1, 10, 3);
    EXPECT_EQ(r.step(), 3);
    EXPECT_EQ(r.count(), 4u); // 1,4,7,10
    EXPECT_EQ(r.at(0), 1);
    EXPECT_EQ(r.at(1), 4);
    EXPECT_EQ(r.at(2), 7);
    EXPECT_EQ(r.at(3), 10);
    EXPECT_EQ(r[0], 1);
    EXPECT_EQ(r[3], 10);
}

TEST(RangeTest, ExplicitStepNotReachingStop) {
    const Range<std::int64_t> r(1, 10, 4);
    EXPECT_EQ(r.count(), 3u); // 1,5,9 (10 недостижимо)
    EXPECT_EQ(r.at(2), 9);
}

TEST(RangeTest, SingleAndEmpty) {
    const Range<std::int64_t> single(5, 5);
    EXPECT_EQ(single.count(), 1u);
    EXPECT_EQ(single.at(0), 5);
    const Range<std::int64_t> empty(1, 10, -1); // обратный шаг, но start < stop
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.count(), 0u);
}

TEST(RangeTest, ContainsIntegral) {
    const Range<std::int64_t> r(1, 10, 2); // 1,3,5,7,9
    EXPECT_TRUE(r.contains(1));
    EXPECT_TRUE(r.contains(7));
    EXPECT_FALSE(r.contains(2));  // нечётный шаг
    EXPECT_FALSE(r.contains(10)); // вне диапазона
    EXPECT_FALSE(r.contains(0));
}

TEST(RangeTest, StepZeroThrows) {
    EXPECT_THROW(Range<std::int64_t>(0, 10, 0), std::invalid_argument);
}

TEST(RangeTest, LazyIteratorForward) {
    const Range<std::int64_t> r(1, 5); // 1..5
    std::vector<std::int64_t> got;
    for (auto it = r.begin(); it != r.end(); ++it) {
        got.push_back(*it);
    }
    const std::vector<std::int64_t> expected{1, 2, 3, 4, 5};
    EXPECT_EQ(got, expected);
    // cbegin/cend и range-for.
    std::vector<std::int64_t> got2;
    for (auto it = r.cbegin(); it != r.cend(); ++it) {
        got2.push_back(*it);
    }
    EXPECT_EQ(got2, expected);
}

TEST(RangeTest, ReversedLazy) {
    const Range<std::int64_t> r(1, 5);
    const auto rev = r.reversed();
    EXPECT_EQ(rev.start(), 5);
    EXPECT_EQ(rev.stop(), 1);
    EXPECT_EQ(rev.step(), -1);
    EXPECT_EQ(rev.count(), 5u);
    EXPECT_EQ(rev.at(0), 5);
    EXPECT_EQ(rev.at(4), 1);
}

TEST(RangeTest, MaterializeVector) {
    const Range<std::int64_t> r(0, 4, 2); // 0,2,4
    const auto v = r.toVector();
    const std::vector<std::int64_t> expected{0, 2, 4};
    EXPECT_EQ(v, expected);
    EXPECT_EQ(r.toArray(), expected);
    EXPECT_EQ(r.toList(), expected);
}

TEST(RangeTest, ToDict) {
    const Range<std::int64_t> r(1, 3); // 1,2,3
    const Dict d = r.toDict();
    EXPECT_EQ(d.size(), 3u);
    EXPECT_EQ(d.at("0").getAs<std::int64_t>(), 1);
    EXPECT_EQ(d.at("1").getAs<std::int64_t>(), 2);
    EXPECT_EQ(d.at("2").getAs<std::int64_t>(), 3);
}

TEST(RangeTest, Equality) {
    const Range<std::int64_t> a(1, 10, 2);
    const Range<std::int64_t> b(1, 10, 2);
    const Range<std::int64_t> c(1, 10, 3);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ── Вещественные ───────────────────────────────────────────────────────────

TEST(RangeTest, FloatDefaultAndStep) {
    const Range<double> r(0.0, 1.0, 0.01);
    EXPECT_DOUBLE_EQ(r.start(), 0.0);
    EXPECT_DOUBLE_EQ(r.stop(), 1.0);
    EXPECT_DOUBLE_EQ(r.step(), 0.01);
    EXPECT_EQ(r.count(), 101u);
    EXPECT_DOUBLE_EQ(r.at(0), 0.0);
    EXPECT_DOUBLE_EQ(r.at(100), 1.0);
    const Range<double> asc(0.0, 1.0);
    EXPECT_DOUBLE_EQ(asc.step(), 1.0);
    EXPECT_EQ(asc.count(), 2u);
}

TEST(RangeTest, FloatContains) {
    const Range<double> r(0.0, 1.0, 0.25); // 0, .25, .5, .75, 1
    EXPECT_TRUE(r.contains(0.0));
    EXPECT_TRUE(r.contains(0.5));
    EXPECT_TRUE(r.contains(1.0));
    EXPECT_FALSE(r.contains(0.3));
}

// ── Рациональные ───────────────────────────────────────────────────────────

TEST(RangeTest, RationalRange) {
    const Range<Rational> r(Rational(0), Rational(100));
    EXPECT_EQ(r.count(), 101u); // 0..100 (инклюзивно)
    EXPECT_EQ(r.at(0).GetAsInteger(), 0);
    EXPECT_EQ(r.at(100).GetAsInteger(), 100);
    EXPECT_TRUE(r.contains(Rational(50)));
    EXPECT_FALSE(r.contains(Rational(101)));
    EXPECT_FALSE(r.contains(Rational(-1)));
}

TEST(RangeTest, RationalFractionalStep) {
    const Range<Rational> r(Rational(0), Rational(1), Rational("1\\3"));
    EXPECT_EQ(r.count(), 4u); // 0, 1/3, 2/3, 1
    EXPECT_EQ(r.at(1).GetAsString(), "1\\3");
    EXPECT_TRUE(r.contains(Rational("2\\3")));
}

// ── Универсальный Any (std::any) ───────────────────────────────────────────

TEST(RangeTest, AnyRange) {
    const Range<std::any> r(std::any(std::int64_t(1)), std::any(std::int64_t(5)));
    EXPECT_EQ(r.count(), 5u);
    EXPECT_DOUBLE_EQ(std::any_cast<double>(r.at(0)), 1.0);
    EXPECT_DOUBLE_EQ(std::any_cast<double>(r.at(4)), 5.0);
    EXPECT_TRUE(r.contains(std::any(std::int64_t(3))));
    EXPECT_FALSE(r.contains(std::any(std::int64_t(6))));
}

TEST(RangeTest, AnyStepZeroThrows) {
    EXPECT_THROW(Range<std::any>(std::any(0), std::any(10), std::any(0)), std::invalid_argument);
}

} // namespace
