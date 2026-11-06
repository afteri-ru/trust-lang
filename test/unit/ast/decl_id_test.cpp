// test/unit/ast/decl_id_test.cpp
// Идентичность объявления (R23): детерминированный id владельца-модуля, идемпотентное назначение,
// независимость от порядка модулей, сохранение при клонировании, политика синтетики.
#include "ast/ast_nodes.hpp"
#include "ast/clone.hpp"
#include "ast/declaration_ids.hpp"
#include "location/decl_id.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace trust {
namespace {

TEST(DeclIdTest, PartsRoundTrip) {
    const DeclId id = DeclId::fromParts(0x12345678u, 7u);
    EXPECT_TRUE(id.isValid());
    EXPECT_EQ(id.ownerHash(), 0x12345678u);
    EXPECT_EQ(id.ordinal(), 7u);
    EXPECT_FALSE(DeclId{}.isValid());
    EXPECT_EQ(DeclId::fromParts(1u, 1u), DeclId::fromParts(1u, 1u));
    EXPECT_NE(DeclId::fromParts(1u, 1u), DeclId::fromParts(1u, 2u));
    EXPECT_NE(DeclId::fromParts(1u, 1u), DeclId::fromParts(2u, 1u));
}

TEST(DeclIdTest, OwnerHashStableAndDistinct) {
    EXPECT_EQ(declarationOwnerHash("\\mod"), declarationOwnerHash("\\mod"));
    EXPECT_NE(declarationOwnerHash("\\a"), declarationOwnerHash("\\b"));
}

TEST(DeclIdTest, AssignIsDeterministicAndIdempotent) {
    auto a = std::make_shared<VarDecl>("a", nullptr, nullptr);
    auto b = std::make_shared<FuncDecl>("b");
    std::vector<AstNodePtr> roots{a, b};

    assignDeclarationIds(roots, "\\mod");
    const DeclId aId = a->declId();
    const DeclId bId = b->declId();
    ASSERT_TRUE(aId.isValid());
    EXPECT_EQ(aId.ownerHash(), declarationOwnerHash("\\mod"));
    EXPECT_EQ(aId.ordinal(), 1u);
    EXPECT_EQ(bId.ordinal(), 2u);

    // Идемпотентно: повторный проход по тому же дереву даёт те же id.
    assignDeclarationIds(roots, "\\mod");
    EXPECT_EQ(a->declId(), aId);
    EXPECT_EQ(b->declId(), bId);
}

TEST(DeclIdTest, IndependentModuleOrderDoesNotAffectIds) {
    auto a = std::make_shared<VarDecl>("a", nullptr, nullptr);
    auto b = std::make_shared<VarDecl>("b", nullptr, nullptr);
    std::vector<AstNodePtr> modA{a};
    std::vector<AstNodePtr> modB{b};

    assignDeclarationIds(modB, "\\b"); // обратный порядок независимых модулей
    assignDeclarationIds(modA, "\\a");

    EXPECT_EQ(a->declId().ownerHash(), declarationOwnerHash("\\a"));
    EXPECT_EQ(b->declId().ownerHash(), declarationOwnerHash("\\b"));
    EXPECT_EQ(a->declId().ordinal(), 1u);
    EXPECT_EQ(b->declId().ordinal(), 1u);
}

TEST(DeclIdTest, ClonePreservesDeclarationId) {
    auto a = std::make_shared<VarDecl>("a", nullptr, nullptr);
    std::vector<AstNodePtr> roots{a};
    assignDeclarationIds(roots, "\\mod");
    ASSERT_TRUE(a->declId().isValid());

    const std::vector<AstNodePtr> clones = cloneTrees(roots);
    ASSERT_EQ(clones.size(), 1u);
    EXPECT_EQ(clones[0]->as<VarDecl>()->declId(), a->declId());
}

TEST(DeclIdTest, SyntheticNodeHasNoId) {
    auto synthetic = std::make_shared<VarDecl>("tmp", nullptr, nullptr);
    EXPECT_FALSE(synthetic->declId().isValid());
}

/// Alias-`TypeDecl` (`MyInt ::= :Int32`) — САМО объявление (нет объявления-ребёнка), поэтому
/// идентичность живёт на обёртке; `declarationIdOf` возвращает её и она переживает клонирование.
TEST(DeclIdTest, AliasTypeDeclCarriesId) {
    auto alias = std::make_shared<Binary>(ParserToken::Kind::TypeDecl);
    alias->m_left = std::make_shared<IdentName>("MyInt");
    alias->m_right = std::make_shared<IdentType>("Int32");
    std::vector<AstNodePtr> roots{alias};

    assignDeclarationIds(roots, "\\mod");
    const DeclId id = alias->declId();
    ASSERT_TRUE(id.isValid());
    EXPECT_EQ(declarationIdOf(*alias), id);
    EXPECT_EQ(id.ownerHash(), declarationOwnerHash("\\mod"));
    EXPECT_EQ(id.ordinal(), 1u);

    const std::vector<AstNodePtr> clones = cloneTrees(roots);
    ASSERT_EQ(clones.size(), 1u);
    EXPECT_EQ(clones[0]->as<Binary>()->declId(), id);
}

/// `TypeDecl` с телом (`Box ::= :Struct{...}`) — объявление-носитель id это `RecordDecl`;
/// у обёртки своей идентичности нет (одна идентичность на объявление).
TEST(DeclIdTest, BodyTypeDeclIdentityBelongsToDeclChild) {
    auto body = std::make_shared<RecordDecl>("Box");
    auto wrapper = std::make_shared<Binary>(ParserToken::Kind::TypeDecl);
    wrapper->m_left = std::make_shared<IdentName>("Box");
    wrapper->m_right = body;
    std::vector<AstNodePtr> roots{wrapper};

    assignDeclarationIds(roots, "\\mod");
    EXPECT_FALSE(wrapper->declId().isValid());
    ASSERT_TRUE(body->declId().isValid());
    EXPECT_EQ(declarationIdOf(*wrapper), body->declId());
}

} // namespace
} // namespace trust
