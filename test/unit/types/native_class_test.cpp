// test/unit/types/native_class_test.cpp
// Юнит-тесты forward-объявления нативных классов (`String ::= %std::string { ... };`):
// регистрация (registerNativeClass, Group::kNativeClass), аксессоры
// (isNativeClassType/nativeClassCppName), дубликат имени, инклуд в preprocIncludes.

#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "diag/context.hpp"
#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class NativeClassFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(NativeClassFixture, RegisterAndAccessors) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId cls = reg.registerNativeClass("MyStr", "std::string", {}, "#include <string>");
    ASSERT_NE(cls, INVALID_TYPE_ID);
    EXPECT_EQ(reg.findType("MyStr"), cls);
    EXPECT_TRUE(reg.isNativeClassType(cls));
    EXPECT_FALSE(reg.isNativeTemplateType(cls));
    EXPECT_EQ(reg.nativeClassCppName(cls), "std::string");
    EXPECT_EQ(reg.getPreprocInclude(cls), "#include <string>");
}

TEST_F(NativeClassFixture, DuplicateName) {
    TypeRegistry& reg = m_ctx.types();
    ASSERT_NE(reg.registerNativeClass("MyStr", "std::string", {}), INVALID_TYPE_ID);
    // Повторная регистрация того же trust-имени → INVALID (диагностика «duplicate type name»).
    EXPECT_EQ(reg.registerNativeClass("MyStr", "std::string", {}), INVALID_TYPE_ID);
}

TEST_F(NativeClassFixture, EmptyCppNameAllowedForPlainName) {
    // RHS может быть любым именем (native = с ведущим '%'); для НЕ-native пустой cppName валиден
    // на уровне реестра (семантика отклоняет использование без C++-имени отдельной диагностикой).
    TypeRegistry& reg = m_ctx.types();
    const TypeId cls = reg.registerNativeClass("Plain", "", {});
    ASSERT_NE(cls, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.isNativeClassType(cls));
    EXPECT_TRUE(reg.nativeClassCppName(cls).empty());
}

} // namespace
} // namespace trust
