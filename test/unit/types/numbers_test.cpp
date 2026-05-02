#include <gtest/gtest.h>
#include "types/forward.hpp"
#include "types/types.hpp"

TEST(FloatTest, default_constructor) {
    trust::Float f;
    EXPECT_EQ(f.kind(), trust::TypeKind::Float64);
    EXPECT_DOUBLE_EQ(f.get(), 0.0);
}

TEST(FloatTest, constructor_with_kind) {
    trust::Float f(3.14, trust::TypeKind::Float32);
    EXPECT_EQ(f.kind(), trust::TypeKind::Float32);
}