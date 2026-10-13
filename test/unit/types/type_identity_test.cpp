// test/unit/types/type_identity_test.cpp
// Контракт отношений identity: structural / semantic / canonical.
// Один и тот же TypeId может по-разному нормализоваться в разных отношениях.

#include "types/type_system.hpp"
#include "type_flag_test_utils.hpp"
#include "types/type_id.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class TypeIdentityFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(TypeIdentityFixture, UserAliasIsNominalInSemanticScope) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType("Int32");
    const TypeId my_int = reg.registrar().registerType("MyInt", int32);
    const TypeId my_int2 = reg.registrar().registerType("MyInt2", my_int);

    EXPECT_EQ(reg.read().store().identityOf(my_int2, TypeIdentityScope::kStructural), my_int2);
    EXPECT_EQ(reg.read().store().identityOf(my_int2, TypeIdentityScope::kSemantic), my_int2);
    EXPECT_EQ(reg.read().store().identityOf(my_int2, TypeIdentityScope::kCanonical), int32);
}

TEST_F(TypeIdentityFixture, BuiltinAliasIsTransparentInSemanticScope) {
    TypeSystem& reg = *m_types;
    const TypeId integer = reg.read().getType("Integer");
    const TypeId int64 = reg.read().getType("Int64");

    EXPECT_EQ(reg.read().store().identityOf(integer, TypeIdentityScope::kSemantic), int64);
    EXPECT_EQ(reg.read().store().identityOf(integer, TypeIdentityScope::kCanonical), int64);
}

TEST_F(TypeIdentityFixture, IdentityScopesIgnoreQualTypeFlags) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType("Int32");
    const TypeId my_int = reg.registrar().registerType("MyInt", int32);
    const TypeId flagged = setFlag(setFlag(my_int, TypeFlag::Const), TypeFlag::Inferred);

    EXPECT_EQ(reg.read().store().identityOf(flagged, TypeIdentityScope::kStructural), my_int);
    EXPECT_EQ(reg.read().store().identityOf(flagged, TypeIdentityScope::kSemantic), my_int);
    EXPECT_EQ(reg.read().store().identityOf(flagged, TypeIdentityScope::kCanonical), int32);
}

TEST_F(TypeIdentityFixture, ScopeResultsMatchNamedOperations) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType("Int32");
    const TypeId my_int = reg.registrar().registerType("MyInt", int32);
    const TypeId flagged = setFlag(my_int, TypeFlag::Inferred);

    EXPECT_EQ(reg.read().store().identityOf(flagged, TypeIdentityScope::kStructural), getUnqualifiedType(flagged));
    EXPECT_EQ(reg.read().store().identityOf(flagged, TypeIdentityScope::kSemantic), reg.read().store().semanticTypeId(flagged));
    EXPECT_EQ(reg.read().store().identityOf(flagged, TypeIdentityScope::kCanonical), reg.read().store().desugaredTypeId(flagged));
}

TEST_F(TypeIdentityFixture, CanonicalReflectsAliasChainRegistration) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType("Int32");
    const TypeId alias_a = reg.registrar().registerType("AliasA", int32);
    const TypeId alias_b = reg.registrar().registerType("AliasB", alias_a);
    const TypeId alias_c = reg.registrar().registerType("AliasC", alias_b);

    EXPECT_EQ(reg.read().store().desugaredTypeId(alias_a), int32);
    EXPECT_EQ(reg.read().store().desugaredTypeId(alias_b), int32);
    EXPECT_EQ(reg.read().store().desugaredTypeId(alias_c), int32);
}

TEST_F(TypeIdentityFixture, LookupAcceptsStringViewWithoutOwnership) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType("Int32");
    const TypeId my_int = reg.registrar().registerType("MyInt", int32);
    const std::string owned = "MyInt";

    EXPECT_EQ(reg.read().findType(std::string_view(owned)), my_int);
    EXPECT_EQ(reg.read().findType(std::string_view("Int32")), int32);
    EXPECT_FALSE(reg.read().findType(std::string_view("NoSuchType")).has_value());
    EXPECT_TRUE(reg.read().names().isBuiltinTypeName(std::string_view("Int32")));
    EXPECT_FALSE(reg.read().names().isBuiltinTypeName(std::string_view("MyInt")));
}

TEST_F(TypeIdentityFixture, AliasDescriptorKeepsImmediateBaseAndCanonicalRoot) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType("Int32");
    const TypeId alias_a = reg.registrar().registerType("AliasA", int32);
    const TypeId alias_b = reg.registrar().registerType("AliasB", alias_a);

    const TypeDescriptor* desc_b = reg.read().store().descriptorOf(alias_b);
    ASSERT_NE(desc_b, nullptr);
    EXPECT_EQ(desc_b->baseType, alias_a);      // immediate target is preserved
    EXPECT_EQ(desc_b->canonicalType, int32);   // canonical root is precomputed

    const TypeDescriptor* desc_a = reg.read().store().descriptorOf(alias_a);
    ASSERT_NE(desc_a, nullptr);
    EXPECT_EQ(desc_a->baseType, int32);
    EXPECT_EQ(desc_a->canonicalType, int32);
}

TEST_F(TypeIdentityFixture, CanonicalAndLookupSurviveReset) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType("Int32");
    const TypeId my_int = reg.registrar().registerType("MyInt", int32);
    ASSERT_TRUE(reg.read().findType("MyInt").has_value());

    reg.reset();

    EXPECT_EQ(reg.read().store().desugaredTypeId(int32), int32);
    EXPECT_FALSE(reg.read().findType("MyInt").has_value());
}

} // namespace
} // namespace trust
