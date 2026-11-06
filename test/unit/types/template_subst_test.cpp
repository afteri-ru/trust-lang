// test/unit/types/template_subst_test.cpp
// Юнит-тесты вынесенных алгоритмов types: подстановка типовых параметров
// (types/subst/template_subst.hpp) и рендеринг C++-имени (types/registry/type_printer.hpp).

#include "support/cpp_facts_test_util.hpp"
#include "session/context.hpp"
#include "types/registry/type_printer.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "types/type_system.hpp"
#include "types/subst/template_subst.hpp"
#include "types/type_data.hpp"
#include "types/type_id.hpp"

#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <vector>

namespace trust {
namespace {

class TypesAlgorithmsFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    TargetContract m_target;

    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        test::installTargetFacts(m_ctx, m_target);
    }
};

TEST_F(TypesAlgorithmsFixture, SubstituteTemplateParamInArray) {
    TypeSystem& reg = *m_types;
    const TypeId param = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId i32 = reg.read().getType("Int32");
    const TypeId arr = reg.interner().getOrCreateArrayType(param);

    const TemplateBindings bindings{{param, false, {i32}}};
    EXPECT_EQ(substituteTemplateParams({reg.read(), reg.interner()}, bindings, arr), reg.interner().getOrCreateArrayType(i32));
}

TEST_F(TypesAlgorithmsFixture, PackInSinglePositionIsInvalid) {
    TypeSystem& reg = *m_types;
    const TypeId param = reg.interner().getOrCreateTemplateParamType("T");
    const TemplateBindings pack{{param, true, {reg.read().getType("Int32"), reg.read().getType("Int64")}}};

    EXPECT_EQ(substituteTemplateParams({reg.read(), reg.interner()}, pack, param), INVALID_TYPE_ID);
}

TEST_F(TypesAlgorithmsFixture, SubstituteTemplateArgListSplicesPack) {
    TypeSystem& reg = *m_types;
    const TypeId param = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId i32 = reg.read().getType("Int32");
    const TypeId i64 = reg.read().getType("Int64");
    const TypeId expansion = reg.interner().getOrCreatePackExpansionType(param);
    const TemplateBindings pack{{param, true, {i32, i64}}};

    const std::vector<TypeId> spliced = substituteTemplateArgList({reg.read(), reg.interner()}, pack, expansion);
    ASSERT_EQ(spliced.size(), 2u);
    EXPECT_EQ(spliced[0], i32);
    EXPECT_EQ(spliced[1], i64);
}

TEST_F(TypesAlgorithmsFixture, InstantiateCallSignatureSubstitutesParam) {
    TypeSystem& reg = *m_types;
    const TypeId param = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId i32 = reg.read().getType("Int32");
    const TypeId declared = reg.interner().getOrCreateFunctionType(param, {param});
    const TemplateBindings bindings{{param, false, {i32}}};
    const std::vector<TypeId> argTypes{i32};

    const TypeId effective = instantiateCallSignature({reg.read(), reg.interner()}, declared, bindings, argTypes);
    ASSERT_NE(effective, INVALID_TYPE_ID);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(effective);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, i32);
    ASSERT_EQ(fd->paramTypes.size(), 1u);
    EXPECT_EQ(fd->paramTypes[0], i32);
}

TEST_F(TypesAlgorithmsFixture, RenderBuiltinCppName) {
    TypeSystem& reg = *m_types;
    const auto name = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(reg.read().getType("Int32"));
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "int32_t");
}

TEST_F(TypesAlgorithmsFixture, RenderUserAliasKeepsTrustName) {
    TypeSystem& reg = *m_types;
    const TypeId alias = reg.registrar().registerType("MyInt", reg.read().getType("Int32"));
    ASSERT_NE(alias, INVALID_TYPE_ID);
    const auto name = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(alias);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "MyInt");
}

TEST_F(TypesAlgorithmsFixture, RenderFunctionType) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType("Int32");
    const TypeId fn = reg.interner().getOrCreateFunctionType(i32, {i32});
    const auto name = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(fn);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "std::function<int32_t(int32_t)>");
}

TEST_F(TypesAlgorithmsFixture, RenderAbsentTypeHasNoCppName) {
    TypeSystem& reg = *m_types;
    const auto name = CppTypeRenderer(reg.read(), test::cppFactsViewOf(m_ctx)).render(INVALID_TYPE_ID);
    EXPECT_FALSE(name.has_value());
}

} // namespace
} // namespace trust
