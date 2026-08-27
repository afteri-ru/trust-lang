// test/unit/types/type_alias_test.cpp
// Прямые юнит-тесты TypeRegistry для регистрации нового типа-синонима
// (alias на Int32), проверки C++-имени, канонического типа, дубликатов,
// цепочек алиасов и сброса реестра.

#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "diag/context.hpp"
#include "gtest/gtest.h"

#include <string>

namespace trust {
namespace {

class TypeAliasFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;

    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    TypeId int32Id() { return m_ctx.types().getType("Int32"); }
};

TEST_F(TypeAliasFixture, RegisterSynonymOfInt) {
    TypeRegistry& reg = m_ctx.types();

    // Регистрация нового типа-синонима на базе Int32.
    TypeId alias = reg.registerType("Age", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    // Алиас - отдельный TypeId, но канонически - Int32.
    EXPECT_NE(alias, int32Id());
    EXPECT_EQ(reg.getCanonicalTypeId(alias), int32Id());

    // Тип ищется по имени; C++-имя - у канонического (базового) типа.
    auto found = reg.findType("Age");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, alias);
    EXPECT_EQ(reg.getCppTypeName(reg.getCanonicalTypeId(alias)).value_or(""), "int32_t");
}

TEST_F(TypeAliasFixture, DuplicateRegisterFails) {
    TypeRegistry& reg = m_ctx.types();

    TypeId alias = reg.registerType("Age", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);

    // Повторная регистрация того же имени - ошибка (INVALID_TYPE_ID).
    EXPECT_EQ(reg.registerType("Age", int32Id()), INVALID_TYPE_ID);
}

TEST_F(TypeAliasFixture, AliasChainCanonical) {
    TypeRegistry& reg = m_ctx.types();

    TypeId age = reg.registerType("Age", int32Id());
    ASSERT_NE(age, INVALID_TYPE_ID);
    TypeId myAge = reg.registerType("MyAge", age); // цепочка MyAge → Age → Int32
    ASSERT_NE(myAge, INVALID_TYPE_ID);

    // Канонический тип разворачивает всю цепочку до Int32.
    EXPECT_EQ(reg.getCanonicalTypeId(myAge), int32Id());
    EXPECT_EQ(reg.getCppTypeName(reg.getCanonicalTypeId(myAge)).value_or(""), "int32_t");
}

TEST_F(TypeAliasFixture, ResetClearsUserAliases) {
    TypeRegistry& reg = m_ctx.types();

    TypeId alias = reg.registerType("Age", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.findType("Age").has_value());

    // reset() очищает пользовательские алиасы, но builtin-типы остаются.
    reg.reset();
    EXPECT_FALSE(reg.findType("Age").has_value());
    EXPECT_TRUE(reg.findType("Int32").has_value());
}

TEST_F(TypeAliasFixture, GetFullTypeNameUserAlias) {
    TypeRegistry& reg = m_ctx.types();

    TypeId alias = reg.registerType("MyInt", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    // Имя владеющее - getFullTypeName возвращает точное имя, а не мусор (латентный баг string_view).
    EXPECT_EQ(reg.getFullTypeName(alias), "MyInt");
    // Пользовательский алиас - это пользовательский тип.
    EXPECT_TRUE(reg.isUserDefinedType(alias));
}

TEST_F(TypeAliasFixture, IsUserDefinedDistinguishesBuiltinAndUser) {
    TypeRegistry& reg = m_ctx.types();

    // Машинные типы и встроенные алиасы - НЕ пользовательские.
    EXPECT_FALSE(reg.isUserDefinedType(reg.getType("Int32")));
    EXPECT_FALSE(reg.isUserDefinedType(reg.getType("Integer")));
    EXPECT_FALSE(reg.isUserDefinedType(reg.getType("String")));

    // Пользовательский алиас - пользовательский.
    TypeId alias = reg.registerType("MyInt", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.isUserDefinedType(alias));
}

TEST_F(TypeAliasFixture, BuiltinAliasesStayBuiltinAfterReset) {
    TypeRegistry& reg = m_ctx.types();

    TypeId alias = reg.registerType("Age", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    reg.reset();

    // Пользовательский алиас очищен; встроенные остались и по-прежнему не пользовательские.
    EXPECT_FALSE(reg.findType("Age").has_value());
    EXPECT_TRUE(reg.findType("Integer").has_value());
    EXPECT_FALSE(reg.isUserDefinedType(reg.getType("Integer")));
    EXPECT_FALSE(reg.isUserDefinedType(reg.getType("Int32")));
}

} // namespace
} // namespace trust
