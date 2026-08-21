// module_registry_test.cpp - unit tests for ModuleRegistry and ModuleNode
#include "module_loader/module_registry.hpp"
#include "module_loader/module_loader.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "diag/context.hpp"
#include "syntax/term.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <fstream>
#include <filesystem>

namespace trust {

// -- ModuleRegistry tests --

class ModuleRegistryTest : public ::testing::Test {
  protected:
    ModuleRegistry reg;

    void SetUp() override {}
};

TEST_F(ModuleRegistryTest, GetOrLoadCreatesNewRecord) {
    auto idx = reg.getOrLoad("module_a");
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(reg.count(), 1u);
    EXPECT_FALSE(reg.isLoaded(idx)); // cacheApi ещё пуст
}

TEST_F(ModuleRegistryTest, GetOrLoadReturnsExistingAfterLoad) {
    auto idx = reg.getOrLoad("module_a");
    EXPECT_EQ(idx, 0u);

    // Симулируем завершение загрузки: устанавливаем Term-тело
    reg.setBody(idx, Term::Create(TermID::SEQUENCE, "body"));

    auto idx2 = reg.getOrLoad("module_a");
    EXPECT_EQ(idx2, idx); // тот же индекс
    EXPECT_TRUE(reg.isLoaded(idx));
}

TEST_F(ModuleRegistryTest, GetOrLoadCycleDetected) {
    (void)reg.getOrLoad("module_a");
    // Term ещё не установлен - симуляция "в процессе загрузки"
    // Повторный вызов того же модуля -> FAULT
    EXPECT_THROW((void)reg.getOrLoad("module_a"), std::runtime_error);
}

TEST_F(ModuleRegistryTest, IsLoadedByIndexFaultForOutOfRange) {
    EXPECT_THROW((void)reg.isLoaded(100), std::runtime_error);
}

TEST_F(ModuleRegistryTest, MultipleIndependentModules) {
    auto idxA = reg.getOrLoad("module_a");
    auto idxB = reg.getOrLoad("module_b");
    EXPECT_EQ(reg.count(), 2u);
    EXPECT_NE(idxA, idxB);

    // Устанавливаем Term для проверки isLoaded
    reg.setBody(idxA, Term::Create(TermID::SEQUENCE, "api_a"));
    reg.setBody(idxB, Term::Create(TermID::SEQUENCE, "api_b"));

    EXPECT_TRUE(reg.isLoaded(idxA));
    EXPECT_TRUE(reg.isLoaded(idxB));
}

TEST_F(ModuleRegistryTest, ModuleNameReturnsCorrectName) {
    auto idx = reg.getOrLoad("my_module");
    EXPECT_EQ(reg.moduleName(idx), "my_module");
}

TEST_F(ModuleRegistryTest, ModuleNameFaultForOutOfRange) {
    EXPECT_THROW((void)reg.moduleName(100), std::runtime_error);
}

TEST_F(ModuleRegistryTest, InterfaceSetGetHas) {
    auto idx = reg.getOrLoad("iface_mod");
    EXPECT_FALSE(reg.hasInterface(idx));

    std::vector<TermPtr> iface{Term::Create(TermID::NAME, "func"), Term::Create(TermID::NAME, "x")};
    reg.setInterface(idx, iface);
    EXPECT_TRUE(reg.hasInterface(idx));
    ASSERT_EQ(reg.interface(idx).size(), 2u);
    EXPECT_EQ(reg.interface(idx)[0]->getText(), "func");
    EXPECT_EQ(reg.interface(idx)[1]->getText(), "x");
}

TEST_F(ModuleRegistryTest, InterfaceFaultForOutOfRange) {
    EXPECT_THROW((void)reg.interface(100), std::runtime_error);
    EXPECT_THROW((void)reg.hasInterface(100), std::runtime_error);
    EXPECT_THROW((void)reg.setInterface(100, {}), std::runtime_error);
}

TEST_F(ModuleRegistryTest, BodyAstSetGet) {
    auto idx = reg.getOrLoad("bodyast_mod");
    reg.setBodyAst(idx, {});
    EXPECT_TRUE(reg.bodyAst(idx).empty());
    reg.setBodyAst(idx, {std::make_shared<AstNodeAttr>(ParserToken::Kind::Ident)});
    ASSERT_EQ(reg.bodyAst(idx).size(), 1u);
    EXPECT_EQ(reg.bodyAst(idx)[0]->kind(), ParserToken::Kind::Ident);
}

TEST_F(ModuleRegistryTest, BodyAstFaultForOutOfRange) {
    EXPECT_THROW((void)reg.bodyAst(100), std::runtime_error);
    EXPECT_THROW((void)reg.setBodyAst(100, {}), std::runtime_error);
}

TEST_F(ModuleRegistryTest, SetTermFaultForOutOfRange) {
    EXPECT_THROW((void)reg.setBody(100, nullptr), std::runtime_error);
}

TEST_F(ModuleRegistryTest, TermReturnsCorrectContent) {
    auto idx = reg.getOrLoad("test_module");
    reg.setBody(idx, Term::Create(TermID::SEQUENCE, "body_item"));

    const TermPtr& retrieved = reg.body(idx);
    ASSERT_NE(retrieved, nullptr);
}

TEST_F(ModuleRegistryTest, TermFaultForOutOfRange) {
    EXPECT_THROW((void)reg.body(100), std::runtime_error);
}

TEST_F(ModuleRegistryTest, TermFaultForNullTerm) {
    auto idx = reg.getOrLoad("test_module");
    // body не установлен
    EXPECT_THROW((void)reg.body(idx), std::runtime_error);
}

// -- ModuleNode tests --

TEST(ModuleNodeTest, CreateModuleNodeFromIndex) {
    ModuleRegistry reg;
    auto idx = reg.getOrLoad("test_module");
    ASSERT_EQ(idx, 0u);

    auto node = std::make_shared<ModuleNode>(idx, "test_module");
    ASSERT_NE(node, nullptr);

    EXPECT_EQ(node->moduleIndex(), idx);
    EXPECT_EQ(node->moduleId(), "test_module");
    EXPECT_EQ(node->kind(), ParserToken::Kind::ModuleDecl);
    EXPECT_TRUE(node->m_body.empty()); // тело заполняется отдельно, не из реестра
}

TEST(ModuleNodeTest, MultipleNodesShareSameModuleIndex) {
    ModuleRegistry reg;
    auto idx = reg.getOrLoad("shared_module");

    auto node1 = std::make_shared<ModuleNode>(idx, "shared_module");
    auto node2 = std::make_shared<ModuleNode>(idx, "shared_module");

    ASSERT_NE(node1, nullptr);
    ASSERT_NE(node2, nullptr);
    EXPECT_EQ(node1->moduleIndex(), node2->moduleIndex()); // одинаковый индекс
    EXPECT_EQ(node1->moduleId(), node2->moduleId());
    EXPECT_NE(node1.get(), node2.get()); // но разные объекты

    // m_body пуста, т.к. не заполнена явно
    EXPECT_TRUE(node1->m_body.empty());
    EXPECT_TRUE(node2->m_body.empty());
}

TEST(ModuleNodeTest, ModuleNodeWithEmptyBody) {
    ModuleRegistry reg;
    auto idx = reg.getOrLoad("empty_module");

    // cacheBody не устанавливаем - тело будет пустым
    auto node = std::make_shared<ModuleNode>(idx, "empty_module");
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->m_body.empty());
}

// -- ModuleLoader via Context tests --

TEST(ModuleLoaderContextTest, ContextProvidesLoader) {
    Context ctx;
    // Context не владеет ModuleLoader - внедряем его явно (как это делает Pipeline).
    ModuleLoader loader(ctx);
    ctx.setLoader(&loader);

    ModuleLoader& l = ctx.loader();
    EXPECT_THROW((void)l.isLoaded(100), std::runtime_error);
}

TEST(LoaderRegistryTest, DirectRegistryUsage) {
    ModuleRegistry reg;
    EXPECT_EQ(reg.count(), 0u);

    auto idx = reg.getOrLoad("ctx_module");
    EXPECT_EQ(reg.count(), 1u);
    EXPECT_EQ(idx, 0u);
}

} // namespace trust