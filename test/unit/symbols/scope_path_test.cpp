#include "symbols/scope_path.hpp"

#include "gtest/gtest.h"

#include <string>
#include <vector>

namespace trust {
namespace {

// -- scope_path: единственная точка разбора текста области имён -------------------------------

std::vector<std::string> segments(std::string_view text) {
    std::vector<std::string> out;
    for (const std::string_view segment : namespaceSegmentsOf(text)) {
        out.emplace_back(segment);
    }
    return out;
}

TEST(ScopePath, SplitsTrailingAndLeadingSeparators) {
    EXPECT_EQ(segments("ns::name::"), (std::vector<std::string>{"ns", "name"}));
    EXPECT_EQ(segments("::ns::name"), (std::vector<std::string>{"ns", "name"}));
    EXPECT_EQ(segments("ns"), (std::vector<std::string>{"ns"}));
    EXPECT_EQ(segments("a::::b"), (std::vector<std::string>{"a", "b"}));
}

TEST(ScopePath, EmptyAndSeparatorOnlyTextHaveNoSegments) {
    EXPECT_TRUE(segments("").empty());
    EXPECT_TRUE(segments("::").empty());
    EXPECT_TRUE(segments("::::").empty());
}

} // namespace
} // namespace trust
