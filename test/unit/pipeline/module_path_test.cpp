// module_path_test.cpp - unit tests for ModulePath (единственный парсер/сборщик spelling пути)
#include "location/module_path.hpp"

#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace trust {
namespace {

TEST(ModulePathTest, ParsesAllPrefixKinds) {
    const auto file = ModulePath::parse("\\a");
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->kind, ModulePath::Kind::File);
    EXPECT_EQ(file->components, (std::vector<std::string>{"a"}));
    EXPECT_TRUE(file->package.empty());
    EXPECT_EQ(file->prefixLength(), 1u);

    const auto group = ModulePath::parse("\\\\grp\\a");
    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(group->kind, ModulePath::Kind::Group);
    EXPECT_EQ(group->components, (std::vector<std::string>{"grp", "a"}));
    EXPECT_EQ(group->relative(), "grp/a");

    const auto package = ModulePath::parse("\\\\\\pkg\\grp\\a");
    ASSERT_TRUE(package.has_value());
    EXPECT_EQ(package->kind, ModulePath::Kind::Package);
    EXPECT_EQ(package->package, "pkg");
    EXPECT_EQ(package->components, (std::vector<std::string>{"grp", "a"}));
    EXPECT_EQ(package->relative(), "grp/a");

    const auto whole_package = ModulePath::parse("\\\\\\pkg");
    ASSERT_TRUE(whole_package.has_value());
    EXPECT_EQ(whole_package->kind, ModulePath::Kind::Package);
    EXPECT_EQ(whole_package->package, "pkg");
    EXPECT_TRUE(whole_package->components.empty());
}

TEST(ModulePathTest, RejectsMalformedSpellings) {
    for (const std::string& spelling : {"a", "", R"(\)", R"(\\)", R"(\\\)", R"(\\\\)", R"(\a\)", R"(\\a\\b)", R"(\\\a\\b)", R"(\\\a\)"}) {
        EXPECT_FALSE(ModulePath::parse(spelling).has_value()) << "must reject: " << spelling;
    }
}

TEST(ModulePathTest, ToStringRoundTripsParse) {
    for (const std::string& spelling : {"\\a", "\\\\grp\\a", "\\\\\\pkg", "\\\\\\pkg\\grp\\a", "\\\\\\pkg\\a_b"}) {
        const auto parsed = ModulePath::parse(spelling);
        ASSERT_TRUE(parsed.has_value()) << spelling;
        EXPECT_EQ(parsed->toString(), spelling);
    }
}

TEST(ModulePathTest, ComponentsWithUnderscoreStayDistinct) {
    const auto first = ModulePath::parse("\\\\a\\b_c");
    const auto second = ModulePath::parse("\\\\a_b\\c");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first->toString(), second->toString());
    EXPECT_EQ(first->relative(), "a/b_c");
    EXPECT_EQ(second->relative(), "a_b/c");
}

} // namespace
} // namespace trust
