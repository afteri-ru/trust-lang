// test/unit/types/type_alias_test.cpp
// Прямые юнит-тесты TypeSystem для регистрации нового типа-синонима
// (alias на Int32), проверки C++-имени, канонического типа, дубликатов,
// цепочек алиасов и сброса реестра.

#include "support/cpp_facts_test_util.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "types/type_id.hpp"
#include "session/context.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "gtest/gtest.h"

#include <string>

namespace trust {
namespace {

class TypeAliasFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    TargetContract m_target;

    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        test::installTargetFacts(m_ctx, m_target);
    }

    TypeId int32Id() { return m_ctx.readTypes().getType("Int32"); }
};

TEST_F(TypeAliasFixture, RegisterSynonymOfInt) {
    TypeSystem& reg = *m_types;

    // Регистрация нового типа-синонима на базе Int32.
    TypeId alias = reg.registrar().registerType("Age", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    // Алиас - отдельный TypeId, но канонически - Int32.
    EXPECT_NE(alias, int32Id());
    EXPECT_EQ(reg.read().store().desugaredTypeId(alias), int32Id());

    // Тип ищется по имени; C++-имя - у канонического (базового) типа.
    auto found = reg.read().findType("Age");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, alias);
    EXPECT_EQ(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(reg.read().store().desugaredTypeId(alias)).value_or(""), "int32_t");
}

TEST_F(TypeAliasFixture, DuplicateRegisterFails) {
    TypeSystem& reg = *m_types;

    TypeId alias = reg.registrar().registerType("Age", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);

    // Повторная регистрация того же имени - ошибка (INVALID_TYPE_ID).
    EXPECT_EQ(reg.registrar().registerType("Age", int32Id()), INVALID_TYPE_ID);
}

TEST_F(TypeAliasFixture, AliasChainCanonical) {
    TypeSystem& reg = *m_types;

    TypeId age = reg.registrar().registerType("Age", int32Id());
    ASSERT_NE(age, INVALID_TYPE_ID);
    TypeId myAge = reg.registrar().registerType("MyAge", age); // цепочка MyAge → Age → Int32
    ASSERT_NE(myAge, INVALID_TYPE_ID);

    // Канонический тип разворачивает всю цепочку до Int32.
    EXPECT_EQ(reg.read().store().desugaredTypeId(myAge), int32Id());
    EXPECT_EQ(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(reg.read().store().desugaredTypeId(myAge)).value_or(""), "int32_t");
}

TEST_F(TypeAliasFixture, ResetClearsUserAliases) {
    TypeSystem& reg = *m_types;

    TypeId alias = reg.registrar().registerType("Age", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().findType("Age").has_value());

    // reset() очищает пользовательские алиасы, но builtin-типы остаются.
    reg.reset();
    EXPECT_FALSE(reg.read().findType("Age").has_value());
    EXPECT_TRUE(reg.read().findType("Int32").has_value());
}

TEST_F(TypeAliasFixture, GetFullTypeNameUserAlias) {
    TypeSystem& reg = *m_types;

    TypeId alias = reg.registrar().registerType("MyInt", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    // Имя владеющее - typeNameOf возвращает точное имя, а не мусор (латентный баг string_view).
    EXPECT_EQ(reg.read().printer().typeNameOf(alias), "MyInt");
    // Пользовательский алиас - это пользовательский тип.
    EXPECT_TRUE(reg.read().store().isUserDefinedType(alias));
}

TEST_F(TypeAliasFixture, IsUserDefinedDistinguishesBuiltinAndUser) {
    TypeSystem& reg = *m_types;

    // Машинные типы и встроенные алиасы - НЕ пользовательские.
    EXPECT_FALSE(reg.read().store().isUserDefinedType(reg.read().getType("Int32")));
    EXPECT_FALSE(reg.read().store().isUserDefinedType(reg.read().getType("Integer")));
    EXPECT_FALSE(reg.read().store().isUserDefinedType(reg.read().getType("String")));

    // Пользовательский алиас - пользовательский.
    TypeId alias = reg.registrar().registerType("MyInt", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().store().isUserDefinedType(alias));
}

TEST_F(TypeAliasFixture, BuiltinAliasesStayBuiltinAfterReset) {
    TypeSystem& reg = *m_types;

    TypeId alias = reg.registrar().registerType("Age", int32Id());
    ASSERT_NE(alias, INVALID_TYPE_ID);
    reg.reset();

    // Пользовательский алиас очищен; встроенные остались и по-прежнему не пользовательские.
    EXPECT_FALSE(reg.read().findType("Age").has_value());
    EXPECT_TRUE(reg.read().findType("Integer").has_value());
    EXPECT_FALSE(reg.read().store().isUserDefinedType(reg.read().getType("Integer")));
    EXPECT_FALSE(reg.read().store().isUserDefinedType(reg.read().getType("Int32")));
}

TEST_F(TypeAliasFixture, AliasTypeNameReusesTargetId) {
    TypeSystem& reg = *m_types;

    const TypeId target = reg.registrar().registerType("MyInt", int32Id());
    ASSERT_NE(target, INVALID_TYPE_ID);

    // `:Alias = MyInt;` - ВТОРОЕ имя существующего типа: TypeId НЕ создаётся.
    const TypeId alias = reg.names().aliasTypeName(target, exportedKey("", "Alias"), "Alias", {});
    ASSERT_EQ(alias, target);
    EXPECT_EQ(m_ctx.diag().errorCount(), 0u);

    // Имя ищется под новым ключом, но display-имя и C++-представление - у цели.
    EXPECT_EQ(reg.read().findType(exportedKey("", "Alias")).value_or(INVALID_TYPE_ID), target);
    EXPECT_EQ(reg.read().printer().typeNameOf(alias), "MyInt");
    EXPECT_EQ(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(alias).value_or(""), "MyInt");
    EXPECT_FALSE(reg.read().store().isFilePrivate(alias));
}

TEST_F(TypeAliasFixture, AliasTypeNameRejectsTakenName) {
    TypeSystem& reg = *m_types;

    const TypeId age = reg.registrar().registerType("Age", int32Id());
    ASSERT_NE(age, INVALID_TYPE_ID);

    // Имя занято другим типом: пользовательская диагностика (не EXPECT), второго имени нет.
    EXPECT_EQ(reg.names().aliasTypeName(int32Id(), exportedKey("", "Age"), "Age", {}), INVALID_TYPE_ID);
    EXPECT_EQ(m_ctx.diag().errorCount(), 1u);
    // Прежняя привязка имени не изменилась (ключ не переприсвоен другому TypeId).
    EXPECT_EQ(reg.read().findType(exportedKey("", "Age")).value_or(INVALID_TYPE_ID), age);
}

} // namespace
} // namespace trust
