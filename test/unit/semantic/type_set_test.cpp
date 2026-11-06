// test/unit/semantic/type_set_test.cpp
// Юнит-тесты проверки наборов допустимых типов (semantic/typer/type_set.hpp):
// правило подтипа для исключения '-' (равенство / одна Group / Record-baseClasses).
#include "semantic/typer/type_set.hpp"

#include "session/context.hpp"
#include "types/group.hpp"
#include "types/type_system.hpp"
#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class TypeSetSubtypeFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(TypeSetSubtypeFixture, Equal) {
    TypeSystem& reg = *m_types;
    const TypeId i8 = reg.read().getType("Int8");
    ASSERT_NE(i8, INVALID_TYPE_ID);
    EXPECT_TRUE(semantic::isTypeSetSubtype(reg, i8, i8));
}

TEST_F(TypeSetSubtypeFixture, WidthOrdering) {
    TypeSystem& reg = *m_types;
    const TypeId i8 = reg.read().getType("Int8");
    const TypeId i32 = reg.read().getType("Int32");
    const TypeId i64 = reg.read().getType("Int64");
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
    TypeSystem& reg = *m_types;
    const TypeId i8 = reg.read().getType("Int8");
    const TypeId f64 = reg.read().getType("Float64");
    ASSERT_NE(i8, INVALID_TYPE_ID);
    ASSERT_NE(f64, INVALID_TYPE_ID);
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, f64, i8));
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, i8, f64));
}

TEST_F(TypeSetSubtypeFixture, Invalid) {
    TypeSystem& reg = *m_types;
    const TypeId i8 = reg.read().getType("Int8");
    ASSERT_NE(i8, INVALID_TYPE_ID);
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, INVALID_TYPE_ID, i8));
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, i8, INVALID_TYPE_ID));
}

TEST_F(TypeSetSubtypeFixture, RecordInheritance) {
    TypeSystem& reg = *m_types;
    const TypeId base = reg.registrar().defineRecord("TBase", Group::kStructs, {});
    const TypeId derived = reg.registrar().defineRecord("TDerived", Group::kStructs, {}, {BaseClass{base, Visibility::Public}});
    ASSERT_NE(base, INVALID_TYPE_ID);
    ASSERT_NE(derived, INVALID_TYPE_ID);
    EXPECT_TRUE(semantic::isTypeSetSubtype(reg, derived, base));
    EXPECT_FALSE(semantic::isTypeSetSubtype(reg, base, derived));
}

} // namespace
} // namespace trust
