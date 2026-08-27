// module_export_test.cpp - тесты сбора экспорт-интерфейса и glob-фильтра модулей
#include "module_loader/module_export.hpp"
#include "module_loader/module_loader.hpp"
#include "ast/term_to_ast.hpp"
#include "diag/context.hpp"
#include "syntax/term.h"

#include <gtest/gtest.h>
#include <vector>

namespace trust {
namespace {

TEST(ModuleExportTest, GlobExactAndWildcards) {
    EXPECT_TRUE(matchGlob("func", "func"));
    EXPECT_FALSE(matchGlob("func", "func2"));
    EXPECT_TRUE(matchGlob("func*", "func2"));
    EXPECT_TRUE(matchGlob("f*", "func"));
    EXPECT_TRUE(matchGlob("f?nc", "func"));
    EXPECT_FALSE(matchGlob("f?nc", "func2"));
    EXPECT_TRUE(matchGlob("*", "anything"));
    EXPECT_TRUE(matchGlob("a*b*c", "axxbyyc"));
    EXPECT_TRUE(matchGlob("", ""));
    EXPECT_FALSE(matchGlob("", "x"));
}

TEST(ModuleExportTest, GlobAnyMask) {
    EXPECT_TRUE(matchesAnyMask("func, x", "x"));
    EXPECT_TRUE(matchesAnyMask("func, x", "func"));
    EXPECT_FALSE(matchesAnyMask("func, x", "y"));
    EXPECT_TRUE(matchesAnyMask(" na*e , other", "name"));
    EXPECT_TRUE(matchesAnyMask("", "anything")); // пустой фильтр = всё
    EXPECT_TRUE(matchesAnyMask("   ", "anything"));
    EXPECT_TRUE(matchesAnyMask("a,,b", "a")); // пустая маска между запятыми = без фильтра
}

// -- Интеграция: сбор экспортов из загруженного модуля --

class ModuleExportIntegrationTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_ctx = std::make_unique<Context>();
        m_loader = std::make_unique<ModuleLoader>(*m_ctx);
        m_ctx->setLoader(m_loader.get());
    }

    std::vector<TermPtr> collect(const char* src, const char* masks) {
        MapperFile modSrc = m_ctx->source().add_source("\\mod", src);
        std::size_t idx = m_ctx->loader().parseSourceModule("\\mod", modSrc);
        std::vector<AstNodePtr> body;
        convertModuleBody(*m_ctx, m_ctx->loader().body(idx), body);
        return collectExportedDecls(body, masks);
    }

    std::unique_ptr<Context> m_ctx;
    std::unique_ptr<ModuleLoader> m_loader;
};

TEST_F(ModuleExportIntegrationTest, CollectsGlobalExportsExcludingAnonymous) {
    // x и func - экспортируемые; hidden в анонимной области `_` - нет.
    auto all = collect("x:Int32 := 42;\n%func():Int32 := { 42 };\n_ { hidden:Int32 := 1; };\n", "");
    EXPECT_EQ(all.size(), 2u);
}

TEST_F(ModuleExportIntegrationTest, FilterByMask) {
    auto funcs = collect("x:Int32 := 42;\n%func():Int32 := { 42 };\n", "func");
    ASSERT_EQ(funcs.size(), 1u);
    // Имя функции - без `%`-префикса в сопоставлении масок; терм-источник нативного func.
    auto x = collect("x:Int32 := 42;\n%func():Int32 := { 42 };\n", "x");
    ASSERT_EQ(x.size(), 1u);
    auto none = collect("x:Int32 := 42;\n", "nope");
    EXPECT_TRUE(none.empty());
}

TEST_F(ModuleExportIntegrationTest, FilterWildcard) {
    auto matches = collect("alpha:Int32 := 1;\nalpha2:Int32 := 2;\nbeta:Int32 := 3;\n", "alpha*");
    EXPECT_EQ(matches.size(), 2u);
}

} // namespace
} // namespace trust