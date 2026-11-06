// test/unit/types/builtin_definition_test.cpp
// Предобъявление встроенного типа и определение его реализации:
// определение не создаёт новый TypeId, переопределяет cppName/includes, сбрасывается reset().

#include "support/cpp_facts_test_util.hpp"
#include "types/type_system.hpp"
#include "type_flag_test_utils.hpp"
#include "types/type_id.hpp"
#include "session/context.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "gtest/gtest.h"

#include <string>
#include <string_view>

namespace trust {
namespace {

class BuiltinImplementationFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    TargetContract m_target;

    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        test::installTargetFacts(m_ctx, m_target);
    }

    TypeId builtin(std::string_view name) { return m_ctx.readTypes().getType(name); }
};

TEST_F(BuiltinImplementationFixture, BuiltinIsPredeclared) {
    TypeSystem& reg = *m_types;
    const TypeId rational = builtin("Rational");
    EXPECT_TRUE(reg.read().store().isBuiltinType(rational));
    EXPECT_TRUE(reg.read().names().isBuiltinTypeName("Rational"));
    EXPECT_FALSE(reg.read().names().isBuiltinTypeName("NoSuchBuiltin"));
}

TEST_F(BuiltinImplementationFixture, DefinitionOverridesImplementation) {
    TypeSystem& reg = *m_types;
    const TypeId rational = builtin("Rational");

    ASSERT_EQ(test::defineBuiltinImplChecked(reg.read(), m_target, "Rational", "custom::Rational", {"@trust/custom_rational.hpp"}), rational);
    EXPECT_EQ(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(rational).value_or(""), "custom::Rational");
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).preprocIncludeOf(rational), "@trust/custom_rational.hpp");
    // Новый TypeId не создан: имя по-прежнему указывает на тот же предобъявленный тип.
    EXPECT_EQ(reg.read().getType("Rational"), rational);
}

TEST_F(BuiltinImplementationFixture, DefinitionRequiresPredeclaration) {
    TypeSystem& reg = *m_types;
    EXPECT_EQ(test::defineBuiltinImplChecked(reg.read(), m_target, "NoSuchBuiltin", "custom::X", {}), INVALID_TYPE_ID);
}

TEST_F(BuiltinImplementationFixture, UserAliasIsNotBuiltin) {
    TypeSystem& reg = *m_types;
    const TypeId myInt = reg.registrar().registerType("MyInt", builtin("Int32"));
    ASSERT_NE(myInt, INVALID_TYPE_ID);
    EXPECT_FALSE(reg.read().store().isBuiltinType(myInt));
    EXPECT_EQ(test::defineBuiltinImplChecked(reg.read(), m_target, "MyInt", "custom::MyInt", {}), INVALID_TYPE_ID);
}

TEST_F(BuiltinImplementationFixture, DefinitionLookupIgnoresTypeFlags) {
    TypeSystem& reg = *m_types;
    ASSERT_NE(test::defineBuiltinImplChecked(reg.read(), m_target, "StrChar", "std::string", {"#include <string>"}), INVALID_TYPE_ID);
    const TypeId str = builtin("StrChar");
    const TypeId inferred = setFlag(str, TypeFlag::Inferred);
    EXPECT_EQ(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(inferred).value_or(""), "std::string");
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).preprocIncludeOf(inferred), "#include <string>");
}

TEST_F(BuiltinImplementationFixture, DefinitionStoresFullImplementation) {
    TypeSystem& reg = *m_types;
    const TypeId rational = builtin("Rational");
    // После переноса реализации в stdlib у ядра нет cppName/include: определение хранит их целиком.
    ASSERT_EQ(test::defineBuiltinImplChecked(reg.read(), m_target, "Rational", "trust::Rational", {"@stdlib/builtin/rational.hpp"}), rational);
    const CppTypeFacts* def = test::cppFactsViewOf(m_ctx).effective(rational);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->cppName, "trust::Rational");
    ASSERT_EQ(def->preprocIncludes.size(), 1u);
    EXPECT_EQ(def->preprocIncludes.front(), "@stdlib/builtin/rational.hpp");
    EXPECT_EQ(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(rational).value_or(""), "trust::Rational");
}

TEST_F(BuiltinImplementationFixture, ResetKeepsPredeclarationFactsArePerRun) {
    TypeSystem& reg = *m_types;
    const TypeId rational = builtin("Rational");
    ASSERT_EQ(test::defineBuiltinImplChecked(reg.read(), m_target, "Rational", "custom::Rational", {"@trust/custom.hpp"}), rational);
    EXPECT_EQ(CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(rational).value_or(""), "custom::Rational");

    reg.reset();
    // Предобъявление встроенного типа сохраняется в реестре типов.
    EXPECT_TRUE(reg.read().store().isBuiltinType(reg.read().getType("Rational")));
    // Целевые C++-факты прогона живут в контракте цели (`TargetContract`, per-run, создаётся
    // `Pipeline`), а НЕ в реестре типов: `reset()` реестра их не трогает. Свежий контракт прогона
    // (как у нового `Pipeline`) определения провайдера не несёт — провайдер приходит только из
    // stdlib-модуля (R1/R37).
    TargetContract freshTarget;
    m_ctx.setTarget(&freshTarget);
    EXPECT_TRUE(test::cppFactsViewOf(m_ctx).declaredCppName(reg.read().getType("Rational")).empty());
}

} // namespace
} // namespace trust
