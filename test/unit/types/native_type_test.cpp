// test/unit/types/native_type_test.cpp
// Регистрация нативных типов: forward-класс (`X ::= %std::string { ... };`) и пользовательский
// шаблон (`<T> vector ::= %std::vector { };`). Один контракт: зона NamedTypeRegistrar регистрирует
// trust-имя, интернирование инстанциации идёт по C++-аргументам, аксессоры читают дескриптор.
#include "type_registry_fixture.hpp"
#include "types/names.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"

#include "gtest/gtest.h"

namespace trust {
namespace {

class NativeTypeTest : public test::TypeSystemFixture {};

TEST_F(NativeTypeTest, RegistersClassAndTemplateAccessors) {
    TypeSystem& reg = types();
    const TypeId int32 = reg.read().getType(type::Int32);

    // Нативный класс: C++-имя — целевой факт (CppTypeFacts), include — on-use.
    const TypeId cls = test::registerNativeClassFacts(reg, m_target, "MyStr", "std::string", {}, "#include <string>");
    ASSERT_NE(cls, INVALID_TYPE_ID);
    EXPECT_EQ(reg.read().findType("MyStr"), cls);
    EXPECT_TRUE(reg.read().store().isNativeClassType(cls));
    EXPECT_FALSE(reg.read().store().isNativeTemplateType(cls));
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).declaredCppName(cls), "std::string");
    // Единый читательский приоритет C++-имени — TypePrinter::declaredCppName.
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).declaredCppName(cls), "std::string");
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).preprocIncludeOf(cls), "#include <string>");

    // Нативный шаблон: абстрактный узел без аргументов + интернированная инстанциация
    // (языковая идентичность — абстрактный шаблон + аргументы).
    const TypeId tpl = test::registerNativeTemplateFacts(reg, m_target, "vector", "std::vector", {}, "#include <vector>");
    ASSERT_NE(tpl, INVALID_TYPE_ID);
    EXPECT_EQ(reg.read().findType("vector"), tpl);
    EXPECT_TRUE(reg.read().store().isNativeTemplateType(tpl));
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).nativeTemplateCppName(tpl), "std::vector");
    EXPECT_TRUE(reg.read().store().nativeTemplateArgs(tpl).empty());
    // Native template НЕ печатается declaredCppName: имя требует аргументов (бэкенд).
    EXPECT_TRUE(test::cppFactsViewOf(m_ctx).declaredCppName(tpl).empty());

    const TypeId v1 = reg.interner().getOrCreateNativeTemplateType(tpl, {int32});
    const TypeId v2 = reg.interner().getOrCreateNativeTemplateType(tpl, {int32});
    ASSERT_NE(v1, INVALID_TYPE_ID);
    EXPECT_EQ(v1, v2); // одинаковые аргументы → один TypeId
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).nativeTemplateCppName(v1), "std::vector");
    ASSERT_EQ(reg.read().store().nativeTemplateArgs(v1).size(), 1U);
    EXPECT_EQ(reg.read().store().nativeTemplateArgs(v1)[0], int32);
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).preprocIncludeOf(v1), "#include <vector>");
    EXPECT_NE(reg.interner().getOrCreateNativeTemplateType(tpl, {reg.read().getType(type::Float64)}), v1);
}

TEST_F(NativeTypeTest, DuplicateNameIsRejectedForBothKinds) {
    TypeSystem& reg = types();
    ASSERT_NE(test::registerNativeClassFacts(reg, m_target, "MyStr", "std::string", {}), INVALID_TYPE_ID);
    EXPECT_EQ(test::registerNativeClassFacts(reg, m_target, "MyStr", "std::string", {}), INVALID_TYPE_ID);

    ASSERT_NE(test::registerNativeTemplateFacts(reg, m_target, "vector", "std::vector", {}), INVALID_TYPE_ID);
    EXPECT_EQ(test::registerNativeTemplateFacts(reg, m_target, "vector", "std::pair", {}), INVALID_TYPE_ID);
}

TEST_F(NativeTypeTest, EmptyCppNameAllowedForPlainName) {
    // RHS может быть любым именем (native = с ведущим '%'); для НЕ-native пустой cppName валиден
    // на уровне реестра (семантика отклоняет использование без C++-имени отдельной диагностикой).
    TypeSystem& reg = types();
    const TypeId cls = test::registerNativeClassFacts(reg, m_target, "Plain", "", {});
    ASSERT_NE(cls, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().store().isNativeClassType(cls));
    EXPECT_TRUE(test::cppFactsViewOf(m_ctx).declaredCppName(cls).empty());
}

} // namespace
} // namespace trust
