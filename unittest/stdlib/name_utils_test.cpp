#include "stdlib/name_utils.hpp"
#include "gtest/gtest.h"

namespace trust {

// ── short_name ─────────────────────────────────────────────────────
TEST(ShortNameTest, GlobalFunction) {
    EXPECT_EQ(short_name("printf"), "printf");
}

TEST(ShortNameTest, NamespaceFunction) {
    EXPECT_EQ(short_name("std::vector::push_back"), "push_back");
}

TEST(ShortNameTest, DeeplyNested) {
    EXPECT_EQ(short_name("std::_Vector_base::impl::do_something"), "do_something");
}

TEST(ShortNameTest, SingleNamespace) {
    EXPECT_EQ(short_name("std::swap"), "swap");
}

TEST(ShortNameTest, Destructor) {
    EXPECT_EQ(short_name("std::vector::~vector"), "~vector");
}

// ── class_name ─────────────────────────────────────────────────────
TEST(ClassNameTest, GlobalFunction) {
    EXPECT_EQ(class_name("printf"), "printf");
}

TEST(ClassNameTest, NamespaceFunction) {
    EXPECT_EQ(class_name("std::vector::push_back"), "std::vector");
}

TEST(ClassNameTest, SingleNamespace) {
    EXPECT_EQ(class_name("std::swap"), "std");
}

TEST(ClassNameTest, DeeplyNested) {
    EXPECT_EQ(class_name("std::_Vector_base::impl::do_something"), "std::_Vector_base::impl");
}

// ── remove_template_args ───────────────────────────────────────────
TEST(RemoveTemplateArgsTest, NoTemplates) {
    EXPECT_EQ(remove_template_args("std::vector"), "std::vector");
}

TEST(RemoveTemplateArgsTest, SimpleTemplate) {
    EXPECT_EQ(remove_template_args("std::vector<int>"), "std::vector");
}

TEST(RemoveTemplateArgsTest, NestedTemplates) {
    EXPECT_EQ(remove_template_args("std::vector<std::pair<int, double>>"), "std::vector");
}

TEST(RemoveTemplateArgsTest, MultipleLevels) {
    EXPECT_EQ(remove_template_args("std::map<std::string, std::vector<int>>"), "std::map");
}

TEST(RemoveTemplateArgsTest, WithSpaces) {
    EXPECT_EQ(remove_template_args("std::vector< int >"), "std::vector");
}

TEST(RemoveTemplateArgsTest, ComplexNested) {
    EXPECT_EQ(remove_template_args("std::allocator_traits<std::allocator<int>>"), "std::allocator_traits");
}

// ── is_internal_name ───────────────────────────────────────────────
TEST(IsInternalNameTest, PublicName) {
    EXPECT_FALSE(is_internal_name("std::vector::push_back"));
}

TEST(IsInternalNameTest, UnderscoreNamespace) {
    EXPECT_TRUE(is_internal_name("std::_Vector_base"));
}

TEST(IsInternalNameTest, UnderscoreMethod) {
    EXPECT_TRUE(is_internal_name("std::vector::_M_insert"));
}

TEST(IsInternalNameTest, GlobalUnderscore) {
    EXPECT_TRUE(is_internal_name("_IO_printf"));
}

TEST(IsInternalNameTest, DoubleUnderscore) {
    EXPECT_TRUE(is_internal_name("std::__vector_base"));
}

TEST(IsInternalNameTest, CleanName) {
    EXPECT_FALSE(is_internal_name("std::initializer_list"));
}

// ── count_occurrences ──────────────────────────────────────────────
TEST(CountOccurrencesTest, EmptyString) {
    EXPECT_EQ(count_occurrences("", "::"), 0u);
}

TEST(CountOccurrencesTest, NoMatches) {
    EXPECT_EQ(count_occurrences("printf", "::"), 0u);
}

TEST(CountOccurrencesTest, SingleMatch) {
    EXPECT_EQ(count_occurrences("std::vector", "::"), 1u);
}

TEST(CountOccurrencesTest, MultipleMatches) {
    EXPECT_EQ(count_occurrences("std::vector::push_back", "::"), 2u);
}

TEST(CountOccurrencesTest, OverlappingExcluded) {
    EXPECT_EQ(count_occurrences(":::", "::"), 1u);
}

TEST(CountOccurrencesTest, DeepNesting) {
    EXPECT_EQ(count_occurrences("a::b::c::d::e", "::"), 4u);
}

} // namespace trust