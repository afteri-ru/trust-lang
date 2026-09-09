// test/unit/semantic/type_set_test.cpp
// Юнит-тесты проверки наборов допустимых типов (semantic/type_set.hpp):
// правило подтипа для исключения '-' (равенство / одна Group / Record-baseClasses).
#include "semantic/type_set.hpp"

#include "session/context.hpp"
#include "types/group.hpp"
#include "types/registry.hpp"
#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class TypeSetSubtypeFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
};

TEST_F(TypeSetSubtypeFixture, Equal) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId i8 = reg.getType("Int8");
    ASSERT_NE(i8, INVALID_TYPE_ID);
    EXPECT_TRUE(semantic::isTypeSetSubtype(reg, i8, i8));
}

TEST_F(TypeSetSubtypeFixture, WidthOrdering) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId i8 = reg.getType("Int8");
    const TypeId i32 = reg.getType("Int32");
    const TypeId i64 = reg.getType("Int64");
    ASSERT_NE(i8, INVALID_TYPE_ID);
    ASSERT_NE(i32, INVALID_TYPE_ID);
    ASSERT_NE(i64, INVALID_TYPE_ID);
    // Одна группа kIntegers: порядок по ШИРИНЕ (`:Int8` уже `:Int32` уже `:Int64`).
    EXPECT_TRUE(semantic::isTypeSetSubtype(reg, i8, i64));
    EXPECT_TRUE(semantic::isTypeSetSubtype(reg, i32, i64));
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, i64, i8));
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, i32, i8)); // Int32 ШИРЕ Int8 - не подтип
}

TEST_F(TypeSetSubtypeFixture, CrossGroup) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId i8 = reg.getType("Int8");
    const TypeId f64 = reg.getType("Float64");
    ASSERT_NE(i8, INVALID_TYPE_ID);
    ASSERT_NE(f64, INVALID_TYPE_ID);
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, f64, i8));
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, i8, f64));
}

TEST_F(TypeSetSubtypeFixture, Invalid) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId i8 = reg.getType("Int8");
    ASSERT_NE(i8, INVALID_TYPE_ID);
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, INVALID_TYPE_ID, i8));
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, i8, INVALID_TYPE_ID));
}

TEST_F(TypeSetSubtypeFixture, RecordInheritance) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId base = reg.defineRecord("TBase", Group::kStructs, {});
    const TypeId derived = reg.defineRecord("TDerived", Group::kStructs, {}, {base});
    ASSERT_NE(base, INVALID_TYPE_ID);
    ASSERT_NE(derived, INVALID_TYPE_ID);
    EXPECT_TRUE(semantic::isTypeSetSubtype(reg, derived, base));
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, base, derived));
}

} // namespace
} // namespace trust
