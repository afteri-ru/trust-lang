#include "stdlib/api_comparator.hpp"
#include "types/forward.hpp"
#include "gtest/gtest.h"

namespace trust {

// ── version_name ─────────────────────────────────────────────────────
TEST(VersionNameTest, Cpp11) {
    EXPECT_STREQ(ApiComparator::version_name(LanguageVersion::CPP11), "c++11");
}

TEST(VersionNameTest, Cpp17) {
    EXPECT_STREQ(ApiComparator::version_name(LanguageVersion::CPP17), "c++17");
}

TEST(VersionNameTest, Cpp20) {
    EXPECT_STREQ(ApiComparator::version_name(LanguageVersion::CPP20), "c++20");
}

TEST(VersionNameTest, Cpp23) {
    EXPECT_STREQ(ApiComparator::version_name(LanguageVersion::CPP23), "c++23");
}

// ── version_suffix ───────────────────────────────────────────────────
TEST(VersionSuffixTest, Cpp11) {
    EXPECT_EQ(ApiComparator::version_suffix(LanguageVersion::CPP11), "c++11");
}

TEST(VersionSuffixTest, Cpp20) {
    EXPECT_EQ(ApiComparator::version_suffix(LanguageVersion::CPP20), "c++20");
}

// ── pattern_to_filename ──────────────────────────────────────────────
// NOTE: depends on get_search_patterns() from trust_stdlib.cpp which defines
// {"std::vector", "std_vector"} etc.  These tests require that patterns are
// registered. We test the mechanism only.

// ── add_version ascending order ───────────────────────────────────────
TEST(ApiComparatorAddVersionTest, AscendingOrder) {
    ApiComparator comp;
    std::vector<MethodInfo> methods_c11;
    MethodInfo m;
    m.qualified_name = "std::vector::push_back";
    m.return_type = "void";
    m.param_types = {"std::vector::value_type&&"};
    m.normalized_signature = "void(value_type&&)";
    m.category = DeclCategory::Method;
    methods_c11.push_back(m);

    EXPECT_TRUE(comp.add_version("std::vector", LanguageVersion::CPP11, methods_c11));
}

TEST(ApiComparatorAddVersionTest, RejectsDescendingOrder) {
    ApiComparator comp;
    std::vector<MethodInfo> methods_c20;
    MethodInfo m;
    m.qualified_name = "std::vector::push_back";
    m.return_type = "void";
    m.param_types = {"std::vector::value_type&&"};
    m.normalized_signature = "void(value_type&&)";
    m.category = DeclCategory::Method;
    methods_c20.push_back(m);

    EXPECT_TRUE(comp.add_version("std::vector", LanguageVersion::CPP20, methods_c20));

    std::vector<MethodInfo> methods_c11;
    MethodInfo m2;
    m2 = m;
    methods_c11.push_back(m2);

    // Adding CPP11 after CPP20 should fail (versions must be ascending)
    EXPECT_FALSE(comp.add_version("std::vector", LanguageVersion::CPP11, methods_c11));
}

// ── get_patterns ─────────────────────────────────────────────────────
TEST(ApiComparatorGetPatternsTest, ReturnsRegisteredPatterns) {
    ApiComparator comp;
    // Patterns come from get_search_patterns() — test that the method works
    const auto &patterns = comp.get_patterns();
    // Should return the map from trust_stdlib.cpp
    EXPECT_FALSE(patterns.empty());
}

// ── check_compatibility ──────────────────────────────────────────────
TEST(ApiComparatorCompatibilityTest, EmptyIsCompatible) {
    ApiComparator comp;
    EXPECT_TRUE(comp.check_compatibility());
}

} // namespace trust