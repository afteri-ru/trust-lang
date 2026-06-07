// module_registry_test.cpp — unit tests for ModuleRegistry and ModuleNode
#include "pipeline/module_registry.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "diag/context.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <fstream>
#include <filesystem>

namespace trust {

// ── ModuleRegistry tests ──

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

    // Симулируем завершение загрузки: заполняем cacheApi
    auto api = std::make_shared<std::vector<AstNodePtr>>();
    reg.setCacheApi(idx, api);

    auto idx2 = reg.getOrLoad("module_a");
    EXPECT_EQ(idx2, idx); // тот же индекс
    EXPECT_TRUE(reg.isLoaded(idx));
}

TEST_F(ModuleRegistryTest, GetOrLoadCycleDetected) {
    (void)reg.getOrLoad("module_a");
    // cacheApi ещё пуст — симуляция "в процессе загрузки"
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

    // Заполняем cacheApi для проверки isLoaded
    auto apiA = std::make_shared<std::vector<AstNodePtr>>();
    apiA->push_back(std::make_shared<Sequence>(ParserToken::Kind::sequence, "api_a", MapperRange{}));
    reg.setCacheApi(idxA, apiA);

    auto apiB = std::make_shared<std::vector<AstNodePtr>>();
    apiB->push_back(std::make_shared<Sequence>(ParserToken::Kind::sequence, "api_b", MapperRange{}));
    reg.setCacheApi(idxB, apiB);

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

TEST_F(ModuleRegistryTest, SetCacheApiFaultForOutOfRange) {
    EXPECT_THROW((void)reg.setCacheApi(100, nullptr), std::runtime_error);
}

TEST_F(ModuleRegistryTest, SetPreprocessedFaultForOutOfRange) {
    EXPECT_THROW((void)reg.setPreprocessed(100, nullptr), std::runtime_error);
}

TEST_F(ModuleRegistryTest, PreprocessedReturnsCorrectContent) {
    auto idx = reg.getOrLoad("test_module");
    auto body = std::make_shared<std::vector<AstNodePtr>>();
    body->push_back(std::make_shared<Sequence>(ParserToken::Kind::sequence, "body_item", MapperRange{}));
    reg.setPreprocessed(idx, body);

    const std::vector<AstNodePtr>& retrieved = reg.preprocessed(idx);
    ASSERT_EQ(retrieved.size(), 1u);
}

TEST_F(ModuleRegistryTest, PreprocessedFaultForOutOfRange) {
    EXPECT_THROW((void)reg.preprocessed(100), std::runtime_error);
}

TEST_F(ModuleRegistryTest, PreprocessedFaultForNullBody) {
    auto idx = reg.getOrLoad("test_module");
    // preprocessed не установлен
    EXPECT_THROW((void)reg.preprocessed(idx), std::runtime_error);
}

TEST_F(ModuleRegistryTest, CacheApiFaultForOutOfRange) {
    EXPECT_THROW((void)reg.cacheApi(100), std::runtime_error);
}

TEST_F(ModuleRegistryTest, CacheApiFaultForNullApi) {
    auto idx = reg.getOrLoad("test_module");
    // cacheApi не установлен
    EXPECT_THROW((void)reg.cacheApi(idx), std::runtime_error);
}

// ── ModuleNode tests ──

TEST(ModuleNodeTest, CreateModuleNodeFromIndex) {
    ModuleRegistry reg;
    auto idx = reg.getOrLoad("test_module");
    ASSERT_EQ(idx, 0u);

    MapperRange rng;
    auto node = std::make_shared<ModuleNode>(idx, "test_module", rng);
    ASSERT_NE(node, nullptr);

    EXPECT_EQ(node->moduleIndex(), idx);
    EXPECT_EQ(node->moduleId(), "test_module");
    EXPECT_EQ(node->kind(), ParserToken::Kind::ModuleDecl);
    EXPECT_TRUE(node->m_body.empty()); // тело заполняется отдельно, не из реестра
}

TEST(ModuleNodeTest, MultipleNodesShareSameModuleIndex) {
    ModuleRegistry reg;
    auto idx = reg.getOrLoad("shared_module");

    MapperRange rng;
    auto node1 = std::make_shared<ModuleNode>(idx, "shared_module", rng);
    auto node2 = std::make_shared<ModuleNode>(idx, "shared_module", rng);

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

    // cacheBody не устанавливаем — тело будет пустым
    MapperRange rng;
    auto node = std::make_shared<ModuleNode>(idx, "empty_module", rng);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->m_body.empty());
}

// ── ModuleLoader via Context tests ──

TEST(ModuleLoaderContextTest, ContextProvidesLoader) {
    Context ctx;
    ModuleLoader& loader = ctx.loader();

    // Проверяем, что loader доступен (косвенно: isLoaded на несуществующем индексе — FAULT)
    EXPECT_THROW((void)loader.isLoaded(100), std::runtime_error);
}

TEST(LoaderRegistryTest, DirectRegistryUsage) {
    ModuleRegistry reg;
    EXPECT_EQ(reg.count(), 0u);

    auto idx = reg.getOrLoad("ctx_module");
    EXPECT_EQ(reg.count(), 1u);
    EXPECT_EQ(idx, 0u);
}

} // namespace trust