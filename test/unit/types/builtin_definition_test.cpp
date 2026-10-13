// test/unit/types/builtin_definition_test.cpp
// Предобъявление встроенного типа и определение его реализации:
// определение не создаёт новый TypeId, переопределяет cppName/includes, сбрасывается reset().

#include "types/type_system.hpp"
#include "type_flag_test_utils.hpp"
#include "types/type_id.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <string>
#include <string_view>

namespace trust {
namespace {

class BuiltinDefinitionFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;

    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    TypeId builtin(std::string_view name) { return m_ctx.readTypes().getType(name); }
};

TEST_F(BuiltinDefinitionFixture, BuiltinIsPredeclared) {
    TypeSystem& reg = m_ctx.types();
    const TypeId rational = builtin("Rational");
    EXPECT_TRUE(reg.read().store().isBuiltinType(rational));
    EXPECT_TRUE(reg.read().names().isBuiltinTypeName("Rational"));
    EXPECT_FALSE(reg.read().names().isBuiltinTypeName("NoSuchBuiltin"));
}

TEST_F(BuiltinDefinitionFixture, DefinitionOverridesImplementation) {
    TypeSystem& reg = m_ctx.types();
    const TypeId rational = builtin("Rational");

    ASSERT_EQ(reg.builtins().define(reg.read(), "Rational", "custom::Rational", {"@trust/custom_rational.hpp"}), rational);
    EXPECT_EQ(reg.read().printer().getCppTypeName(rational).value_or(""), "custom::Rational");
    EXPECT_EQ(reg.read().builtins().preprocIncludeOf(rational), "@trust/custom_rational.hpp");
    // Новый TypeId не создан: имя по-прежнему указывает на тот же предобъявленный тип.
    EXPECT_EQ(reg.read().getType("Rational"), rational);
}

TEST_F(BuiltinDefinitionFixture, DefinitionRequiresPredeclaration) {
    TypeSystem& reg = m_ctx.types();
    EXPECT_EQ(reg.builtins().define(reg.read(), "NoSuchBuiltin", "custom::X", {}), INVALID_TYPE_ID);
}

TEST_F(BuiltinDefinitionFixture, UserAliasIsNotBuiltin) {
    TypeSystem& reg = m_ctx.types();
    const TypeId myInt = reg.registrar().registerType("MyInt", builtin("Int32"));
    ASSERT_NE(myInt, INVALID_TYPE_ID);
    EXPECT_FALSE(reg.read().store().isBuiltinType(myInt));
    EXPECT_EQ(reg.builtins().define(reg.read(), "MyInt", "custom::MyInt", {}), INVALID_TYPE_ID);
}

TEST_F(BuiltinDefinitionFixture, DefinitionLookupIgnoresTypeFlags) {
    TypeSystem& reg = m_ctx.types();
    ASSERT_NE(reg.builtins().define(reg.read(), "StrChar", "std::string", {"#include <string>"}), INVALID_TYPE_ID);
    const TypeId str = builtin("StrChar");
    const TypeId inferred = setFlag(str, TypeFlag::Inferred);
    EXPECT_EQ(reg.read().printer().getCppTypeName(inferred).value_or(""), "std::string");
    EXPECT_EQ(reg.read().builtins().preprocIncludeOf(inferred), "#include <string>");
}

TEST_F(BuiltinDefinitionFixture, ResetDropsProviderDefinitions) {
    TypeSystem& reg = m_ctx.types();
    const TypeId rational = builtin("Rational");
    ASSERT_EQ(reg.builtins().define(reg.read(), "Rational", "custom::Rational", {"@trust/custom.hpp"}), rational);
    EXPECT_EQ(reg.read().printer().getCppTypeName(rational).value_or(""), "custom::Rational");

    reg.reset();
    // Предобъявление сохраняется, определение провайдера сброшено: снова ядро.
    EXPECT_TRUE(reg.read().store().isBuiltinType(reg.read().getType("Rational")));
    EXPECT_EQ(reg.read().printer().getCppTypeName(reg.read().getType("Rational")).value_or(""), "trust::Rational");
}

} // namespace
} // namespace trust
