// test/unit/sourcemap/range_order_test.cpp
// Порядок MapperRange: begin, затем end. Обязан согласовываться с operator== (R42: порядок
// диагностик/артефактов не должен зависеть от недетерминированных входных последовательностей).
#include "location/location.hpp"

#include <gtest/gtest.h>

namespace trust {
namespace {

TEST(MapperRangeOrder, SameBeginOrdersByEnd) {
    const MapperFile file = MapperFile::make_input(0);
    const MapperLocation begin = MapperLocation::makeLoc(file, 10);
    const MapperRange shorter{begin, MapperLocation::makeLoc(file, 20)};
    const MapperRange longer{begin, MapperLocation::makeLoc(file, 30)};

    EXPECT_NE(shorter, longer);
    EXPECT_TRUE((shorter <=> longer) != 0);
    EXPECT_LT(shorter, longer);
}

TEST(MapperRangeOrder, EqualRangeIsEquivalent) {
    const MapperFile file = MapperFile::make_input(0);
    const MapperRange a{MapperLocation::makeLoc(file, 10), MapperLocation::makeLoc(file, 20)};
    const MapperRange b{MapperLocation::makeLoc(file, 10), MapperLocation::makeLoc(file, 20)};

    EXPECT_EQ(a, b);
    EXPECT_TRUE((a <=> b) == 0);
}

TEST(MapperRangeOrder, InvalidRangesAreEquivalent) {
    const MapperRange a{};
    const MapperRange b{};

    EXPECT_EQ(a, b);
    EXPECT_TRUE((a <=> b) == 0);
}

} // namespace
} // namespace trust