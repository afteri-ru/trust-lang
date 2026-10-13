#include "ast/clone.hpp"
#include "ast/ast_nodes.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

namespace trust {
namespace {

TEST(AliasKindTest, SetAliasKeepsKindAndTargetConsistent) {
    FuncDecl fn(std::string("f"));
    EXPECT_EQ(fn.aliasKind(), AliasKind::None);
    EXPECT_FALSE(fn.aliasTarget());

    auto target = std::make_shared<IdentName>(std::string("g"));
    fn.setAlias(AliasKind::Name, target, {});
    EXPECT_EQ(fn.aliasKind(), AliasKind::Name);
    EXPECT_EQ(fn.aliasTarget().get(), target.get());

    fn.setAlias(AliasKind::None, nullptr, {});
    EXPECT_EQ(fn.aliasKind(), AliasKind::None);
    EXPECT_FALSE(fn.aliasTarget());
}

TEST(AliasKindTest, SetAliasRejectsInconsistentKindAndTarget) {
    FuncDecl fn(std::string("ns::"));
    auto target = std::make_shared<IdentName>(std::string("\\lib::ns"));

    EXPECT_THROW(fn.setAlias(AliasKind::None, target, {}), std::runtime_error);
    EXPECT_THROW(fn.setAlias(AliasKind::Namespace, nullptr, {}), std::runtime_error);
}

TEST(AliasKindTest, ClonePreservesAliasKindAndTarget) {
    auto target = std::make_shared<IdentName>(std::string("g"));
    auto fn = std::make_shared<FuncDecl>(std::string("f"));
    fn->setAlias(AliasKind::Name, target, {});

    std::vector<AstNodePtr> cloned = cloneTrees({fn});
    ASSERT_EQ(cloned.size(), 1u);
    const auto& clone = *cloned.front()->as<FuncDecl>();
    EXPECT_EQ(clone.aliasKind(), AliasKind::Name);
    ASSERT_TRUE(clone.aliasTarget());
    ASSERT_TRUE(clone.aliasTarget()->is<IdentName>());
    EXPECT_EQ(clone.aliasTarget()->as<IdentName>()->text(), "g");
}

} // namespace
} // namespace trust
