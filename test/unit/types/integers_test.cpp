#include <gtest/gtest.h>
#include "types/forward.hpp"
#include "types/types.hpp"

TEST(IntegerTest, default_constructor) {
    trust::Integers i;
    EXPECT_EQ(i.kind(), trust::TypeKind::Int64);
    EXPECT_EQ(i.get(), 0);
}

TEST(IntegerTest, constructor_with_kind) {
    trust::Integers i(42, trust::TypeKind::Int32);
    EXPECT_EQ(i.kind(), trust::TypeKind::Int32);
    EXPECT_EQ(i.get(), 42);
}

TEST(IntegerTest, convert_to) {
    trust::Integers i(123, trust::TypeKind::Int16);
    trust::Integers dest(0, trust::TypeKind::Int16);
    i.convert_to(trust::TypeKind::Int16, dest);
    EXPECT_EQ(dest.get(), 123);
}