#include "types/type_info.hpp"
#include <gtest/gtest.h>
#include <type_traits>

// ============================================================================
// Compile-time Type Traits Tests
// ============================================================================
using namespace trust;

// ---- Static assertions: type_to_kind_v ----

static_assert(type_to_kind_v<int> == TypeKind::Int, "int should map to TypeKind::Int");
static_assert(type_to_kind_v<std::string> == TypeKind::String, "std::string should map to TypeKind::String");
static_assert(type_to_kind_v<bool> == TypeKind::Bool, "bool should map to TypeKind::Bool");

// ---- Static assertions: kind_type_t ----

static_assert(std::is_same_v<kind_type_t<TypeKind::Int>, int>, "TypeKind::Int should map to int");
static_assert(std::is_same_v<kind_type_t<TypeKind::String>, std::string>, "TypeKind::String should map to std::string");
static_assert(std::is_same_v<kind_type_t<TypeKind::Bool>, bool>, "TypeKind::Bool should map to bool");
static_assert(std::is_same_v<kind_type_t<TypeKind::None>, void>, "TypeKind::None should map to void");
static_assert(std::is_same_v<kind_type_t<TypeKind::Expected>, void>, "TypeKind::Expected should map to void (placeholder)");
static_assert(std::is_same_v<kind_type_t<TypeKind::UserType>, void>, "TypeKind::UserType should map to void (placeholder)");

// ---- Static assertions: round-trip ----

static_assert(type_to_kind_v<kind_type_t<TypeKind::Int>> == TypeKind::Int, "round-trip Int");
static_assert(type_to_kind_v<kind_type_t<TypeKind::String>> == TypeKind::String, "round-trip String");
static_assert(type_to_kind_v<kind_type_t<TypeKind::Bool>> == TypeKind::Bool, "round-trip Bool");

// ---- Compile-time if constexpr usage example ----

template <TypeKind K>
constexpr bool is_numeric_type() {
    if constexpr (K == TypeKind::Int || K == TypeKind::Bool) {
        return true;
    } else {
        return false;
    }
}

// ---- Runtime tests ----

class TypeInfoTraitsTest : public ::testing::Test {};

TEST_F(TypeInfoTraitsTest, TypeIndexForInt) {
    auto ti = type_index_for(TypeKind::Int);
    EXPECT_EQ(ti, std::type_index(typeid(int)));
}

TEST_F(TypeInfoTraitsTest, TypeIndexForString) {
    auto ti = type_index_for(TypeKind::String);
    EXPECT_EQ(ti, std::type_index(typeid(std::string)));
}

TEST_F(TypeInfoTraitsTest, TypeIndexForBool) {
    auto ti = type_index_for(TypeKind::Bool);
    EXPECT_EQ(ti, std::type_index(typeid(bool)));
}

TEST_F(TypeInfoTraitsTest, TypeIndexForNoneReturnsVoid) {
    auto ti = type_index_for(TypeKind::None);
    EXPECT_EQ(ti, std::type_index(typeid(void)));
}

TEST_F(TypeInfoTraitsTest, TypeIndexForUserTypeReturnsVoid) {
    auto ti = type_index_for(TypeKind::UserType);
    EXPECT_EQ(ti, std::type_index(typeid(void)));
}

TEST_F(TypeInfoTraitsTest, KindFromTypeIndexInt) {
    auto k = kind_from_type_index(std::type_index(typeid(int)));
    ASSERT_TRUE(k.has_value());
    EXPECT_EQ(*k, TypeKind::Int);
}

TEST_F(TypeInfoTraitsTest, KindFromTypeIndexString) {
    auto k = kind_from_type_index(std::type_index(typeid(std::string)));
    ASSERT_TRUE(k.has_value());
    EXPECT_EQ(*k, TypeKind::String);
}

TEST_F(TypeInfoTraitsTest, KindFromTypeIndexUnknown) {
    auto k = kind_from_type_index(std::type_index(typeid(double)));
    EXPECT_FALSE(k.has_value());
}

TEST_F(TypeInfoTraitsTest, TypeInfoBuiltinIsBuiltin) {
    auto ti = TypeInfo::builtin(TypeKind::Int);
    EXPECT_TRUE(ti.is_builtin());
    EXPECT_FALSE(ti.is_user());
}

TEST_F(TypeInfoTraitsTest, TypeInfoUserIsUser) {
    auto ti = TypeInfo::user("MyType");
    EXPECT_TRUE(ti.is_user());
    EXPECT_FALSE(ti.is_builtin());
}

TEST_F(TypeInfoTraitsTest, TypeInfoEquality) {
    auto a = TypeInfo::builtin(TypeKind::Int);
    auto b = TypeInfo::builtin(TypeKind::Int);
    auto c = TypeInfo::builtin(TypeKind::String);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST_F(TypeInfoTraitsTest, TypeInfoUserEquality) {
    auto a = TypeInfo::user("Foo");
    auto b = TypeInfo::user("Foo");
    auto c = TypeInfo::user("Bar");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST_F(TypeInfoTraitsTest, CompileTimeIfConstexpr) {
    static_assert(is_numeric_type<TypeKind::Int>(), "Int is numeric");
    static_assert(is_numeric_type<TypeKind::Bool>(), "Bool is numeric");
    EXPECT_FALSE((is_numeric_type<TypeKind::String>()));
    EXPECT_FALSE((is_numeric_type<TypeKind::None>()));
}