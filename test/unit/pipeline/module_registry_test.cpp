// module_registry_test.cpp - unit tests for ModuleRegistry and ModuleNode
#include "module_loader/registry/module_registry.hpp"
#include "module_loader/module_loader.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "session/context.hpp"
#include "syntax/term/term.h"
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
    EXPECT_EQ(idx, ModuleIndex{0});
    EXPECT_EQ(reg.count(), 1u);
    EXPECT_FALSE(reg.isLoaded(idx)); // загрузка ещё не завершена (state Loading)
}

TEST_F(ModuleRegistryTest, GetOrLoadReturnsExistingAfterLoad) {
    auto idx = reg.getOrLoad("module_a");
    EXPECT_EQ(idx, ModuleIndex{0});

    // Завершение загрузки: AST сконвертирован
    reg.setBodyAst(idx, {});

    auto idx2 = reg.getOrLoad("module_a");
    EXPECT_EQ(idx2, idx); // тот же индекс
    EXPECT_TRUE(reg.isLoaded(idx));
}

TEST_F(ModuleRegistryTest, GetOrLoadCycleDetected) {
    (void)reg.getOrLoad("module_a");
    // запись в состоянии Loading - симуляция "в процессе загрузки"
    // Повторный вызов того же модуля -> FAULT
    EXPECT_THROW((void)reg.getOrLoad("module_a"), std::runtime_error);
}

TEST_F(ModuleRegistryTest, FindIndexByCanonicalId) {
    auto idx = reg.getOrLoad("canonical_mod");
    auto found = reg.findIndex("canonical_mod");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, idx);
    EXPECT_FALSE(reg.findIndex("missing_mod").has_value());
}

TEST_F(ModuleRegistryTest, IsLoadedByIndexFaultForOutOfRange) {
    EXPECT_THROW((void)reg.isLoaded(ModuleIndex{100}), std::runtime_error);
}

TEST_F(ModuleRegistryTest, MultipleIndependentModules) {
    auto idxA = reg.getOrLoad("module_a");
    auto idxB = reg.getOrLoad("module_b");
    EXPECT_EQ(reg.count(), 2u);
    EXPECT_NE(idxA, idxB);

    // Завершаем загрузку обоих модулей (AST сконвертирован)
    reg.setBodyAst(idxA, {});
    reg.setBodyAst(idxB, {});

    EXPECT_TRUE(reg.isLoaded(idxA));
    EXPECT_TRUE(reg.isLoaded(idxB));
}

TEST_F(ModuleRegistryTest, ModuleNameReturnsCorrectName) {
    auto idx = reg.getOrLoad("my_module");
    EXPECT_EQ(reg.modulePath(idx), "my_module");
    EXPECT_EQ(reg.findIndex("my_module"), std::optional<ModuleIndex>(idx));
    EXPECT_FALSE(reg.findIndex("missing_module").has_value());
}

TEST_F(ModuleRegistryTest, ModuleNameFaultForOutOfRange) {
    EXPECT_THROW((void)reg.modulePath(ModuleIndex{100}), std::runtime_error);
}

TEST_F(ModuleRegistryTest, InterfaceFactsSetGetHas) {
    auto idx = reg.getOrLoad("iface_mod");
    EXPECT_FALSE(reg.hasInterfaceFacts(idx));

    ModuleInterfaceFacts facts;
    facts.entries = {ExportEntry{std::make_shared<IdentName>("func"), "func"}, ExportEntry{std::make_shared<IdentName>("x"), "ns::x"}};
    facts.hasOutOfLineDefinitions = true;
    reg.setInterfaceFacts(idx, facts);
    EXPECT_TRUE(reg.hasInterfaceFacts(idx));
    ASSERT_EQ(reg.interfaceFacts(idx).entries.size(), 2u);
    EXPECT_EQ(reg.interfaceFacts(idx).entries[0].qualifiedName, "func");
    EXPECT_EQ(reg.interfaceFacts(idx).entries[1].qualifiedName, "ns::x");
    EXPECT_EQ(requiredText(*reg.interfaceFacts(idx).entries[1].node), "x");
    EXPECT_TRUE(reg.interfaceFacts(idx).hasOutOfLineDefinitions);
}

TEST_F(ModuleRegistryTest, InterfaceFactsFaultForOutOfRange) {
    EXPECT_THROW((void)reg.interfaceFacts(ModuleIndex{100}), std::runtime_error);
    EXPECT_THROW((void)reg.hasInterfaceFacts(ModuleIndex{100}), std::runtime_error);
    EXPECT_THROW((void)reg.setInterfaceFacts(ModuleIndex{100}, {}), std::runtime_error);
}

TEST_F(ModuleRegistryTest, BodyAstSetGet) {
    auto idx = reg.getOrLoad("bodyast_mod");
    reg.setBodyAst(idx, {});
    EXPECT_TRUE(reg.bodyAst(idx).empty());
    reg.setBodyAst(idx, {std::make_shared<IdentName>()});
    ASSERT_EQ(reg.bodyAst(idx).size(), 1u);
    EXPECT_EQ(reg.bodyAst(idx)[0]->kind(), ParserToken::Kind::Ident);
}

TEST_F(ModuleRegistryTest, BodyAstFaultForOutOfRange) {
    EXPECT_THROW((void)reg.bodyAst(ModuleIndex{100}), std::runtime_error);
    EXPECT_THROW((void)reg.setBodyAst(ModuleIndex{100}, {}), std::runtime_error);
}

TEST_F(ModuleRegistryTest, SetBodyAstFaultForOutOfRange) {
    EXPECT_THROW((void)reg.setBodyAst(ModuleIndex{100}, {}), std::runtime_error);
}

// -- ModuleNode tests --

TEST(ModuleNodeTest, CreateModuleNodeFromIndex) {
    ModuleRegistry reg;
    auto idx = reg.getOrLoad("test_module");
    ASSERT_EQ(idx, ModuleIndex{0});

    auto node = std::make_shared<ModuleNode>(idx, "test_module");
    ASSERT_NE(node, nullptr);

    EXPECT_EQ(node->moduleIndex(), idx);
    EXPECT_EQ(node->importSpelling(), "test_module");
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
    EXPECT_EQ(node1->importSpelling(), node2->importSpelling());
    EXPECT_NE(node1.get(), node2.get()); // но разные объекты

    // m_body пуста, т.к. не заполнена явно
    EXPECT_TRUE(node1->m_body.empty());
    EXPECT_TRUE(node2->m_body.empty());
}

TEST(ModuleNodeTest, ModuleNodeWithEmptyBody) {
    ModuleRegistry reg;
    auto idx = reg.getOrLoad("empty_module");

    // m_body не устанавливаем - тело будет пустым
    auto node = std::make_shared<ModuleNode>(idx, "empty_module");
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->m_body.empty());
}

// -- ModuleLoader via Context tests --

TEST(ModuleLoaderContextTest, ContextProvidesLoader) {
    Context ctx;
    // Context не владеет ModuleLoader - внедряем его явно (как это делает Pipeline).
    ModuleLoader loader(ctx);
    ctx.setModuleResolver(&loader);

    ctx.setModuleResolver(&loader);
    EXPECT_EQ(ctx.moduleResolver(), &loader);
    EXPECT_THROW((void)loader.isLoaded(ModuleIndex{100}), std::runtime_error);
}

TEST(LoaderRegistryTest, DirectRegistryUsage) {
    ModuleRegistry reg;
    EXPECT_EQ(reg.count(), 0u);

    auto idx = reg.getOrLoad("ctx_module");
    EXPECT_EQ(reg.count(), 1u);
    EXPECT_EQ(idx, ModuleIndex{0});
}

} // namespace trust