// test/unit/types/native_template_test.cpp
// Юнит-тесты пользовательских нативных шаблонов-типов (`<T> %std::vector() := ...;`): регистрация
// абстрактного шаблона (registerNativeTemplate), интернирование конкретной инстанциации
// (getOrCreateNativeTemplateType), аксессоры (isNativeTemplateType/nativeTemplateCppName/args),
// дубликат имени, инклуд в preprocIncludes.

#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "diag/context.hpp"
#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class NativeTemplateFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(NativeTemplateFixture, RegisterAndInstantiate) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType(type::Int32);

    // Регистрация абстрактного шаблона `vector` (trust-имя) с C++-именем "std::vector".
    const TypeId tpl = reg.registerNativeTemplate("vector", "std::vector", {});
    ASSERT_NE(tpl, INVALID_TYPE_ID);
    EXPECT_EQ(reg.findType("vector"), tpl);
    EXPECT_TRUE(reg.isNativeTemplateType(tpl));
    EXPECT_EQ(reg.nativeTemplateCppName(tpl), "std::vector");
    EXPECT_TRUE(reg.nativeTemplateArgs(tpl).empty()); // абстрактный шаблон - без аргументов

    // Конкретная инстанциация `vector<Int32>`: интернирование по аргументам.
    const TypeId v1 = reg.getOrCreateNativeTemplateType("std::vector", {int32}, "#include <vector>");
    const TypeId v2 = reg.getOrCreateNativeTemplateType("std::vector", {int32}, "#include <vector>");
    ASSERT_NE(v1, INVALID_TYPE_ID);
    EXPECT_EQ(v1, v2); // одинаковые аргументы → один TypeId
    EXPECT_TRUE(reg.isNativeTemplateType(v1));
    EXPECT_EQ(reg.nativeTemplateCppName(v1), "std::vector");
    ASSERT_EQ(reg.nativeTemplateArgs(v1).size(), 1u);
    EXPECT_EQ(reg.nativeTemplateArgs(v1)[0], int32);
    EXPECT_EQ(reg.getPreprocInclude(v1), "#include <vector>");

    // Другой аргумент → другой тип.
    EXPECT_NE(reg.getOrCreateNativeTemplateType("std::vector", {reg.getType(type::Float64)}), v1);
}

TEST_F(NativeTemplateFixture, DuplicateName) {
    TypeRegistry& reg = m_ctx.types();
    ASSERT_NE(reg.registerNativeTemplate("vector", "std::vector", {}), INVALID_TYPE_ID);
    // Повторная регистрация того же trust-имени → INVALID (диагностика «duplicate type name»).
    EXPECT_EQ(reg.registerNativeTemplate("vector", "std::pair", {}), INVALID_TYPE_ID);
}

} // namespace
} // namespace trust
