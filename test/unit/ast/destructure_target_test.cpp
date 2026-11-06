// test/unit/ast/destructure_target_test.cpp
// Контракт DestructureDecl: per-target состояние (имя, аннотация, типы, rest-флаг) хранится
// одной записью Target; клонирование сохраняет порядок целей и rest-флаги (слот-дрейф TargetList).
#include "ast/ast_nodes.hpp"
#include "ast/clone.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace trust {
namespace {

TEST(DestructureTargetTest, StoresPerTargetStateInOneRecord) {
    DestructureDecl decl;
    decl.m_isSpread = true;

    auto first_name = std::make_shared<IdentName>(std::string("a"));
    DestructureDecl::Target first;
    first.name = first_name;
    decl.m_targets.push_back(first);

    auto rest_name = std::make_shared<IdentName>(std::string("tail"));
    DestructureDecl::Target rest;
    rest.name = rest_name;
    rest.isRest = true;
    decl.m_targets.push_back(std::move(rest));

    ASSERT_EQ(decl.m_targets.size(), 2u);
    EXPECT_EQ(first_name->text(), "a");
    EXPECT_FALSE(decl.m_targets[0].isRest);
    EXPECT_EQ(rest_name->text(), "tail");
    EXPECT_TRUE(decl.m_targets[1].isRest);
}

TEST(DestructureTargetTest, CloneKeepsTargetOrderAndRestFlags) {
    auto decl = std::make_shared<DestructureDecl>();
    decl->m_isSpread = true;

    auto orig_name = std::make_shared<IdentName>(std::string("head"));
    auto orig_type = std::make_shared<IdentName>(std::string("Int32"));
    DestructureDecl::Target entry;
    entry.name = orig_name;
    entry.typeNode = orig_type;
    decl->m_targets.push_back(std::move(entry));

    auto orig_rest = std::make_shared<IdentName>(std::string("rest"));
    DestructureDecl::Target rest_entry;
    rest_entry.name = orig_rest;
    rest_entry.isRest = true;
    decl->m_targets.push_back(std::move(rest_entry));

    std::vector<AstNodePtr> clones = cloneTrees({decl});
    ASSERT_EQ(clones.size(), 1u);
    const auto& clone = static_cast<const DestructureDecl&>(*clones[0]);
    EXPECT_EQ(clone.origin, decl.get());
    ASSERT_EQ(clone.m_targets.size(), 2u);

    const IdentName& cloned_head = *clone.m_targets[0].name->as<IdentName>();
    EXPECT_EQ(cloned_head.text(), "head");
    EXPECT_NE(clone.m_targets[0].name.get(), orig_name.get()) << "target name must be cloned";
    EXPECT_FALSE(clone.m_targets[0].isRest);
    ASSERT_NE(clone.m_targets[0].typeNode, nullptr);
    EXPECT_EQ(clone.m_targets[0].typeNode->as<IdentName>()->text(), "Int32");
    EXPECT_NE(clone.m_targets[0].typeNode.get(), orig_type.get()) << "target type node must be cloned";

    const IdentName& cloned_rest = *clone.m_targets[1].name->as<IdentName>();
    EXPECT_EQ(cloned_rest.text(), "rest");
    EXPECT_TRUE(clone.m_targets[1].isRest);
}

} // namespace
} // namespace trust
