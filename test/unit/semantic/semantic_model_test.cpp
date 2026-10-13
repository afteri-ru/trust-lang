// test/unit/semantic/semantic_model_test.cpp
// Модель семантики — продукт шага Semantic: строится без флагов и сохраняет ЛОКАЛЬНЫЕ
// объявления в persistent-дереве скоупов (после выхода из блока).
#include "pipeline/pipeline.hpp"
#include "semantic/semantic_model.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"
#include "gtest/gtest.h"

#include <cstddef>
#include <string>

namespace trust {
namespace {

TEST(SemanticModel, ProducedBySemanticStepWithoutFlags) {
    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    Pipeline pipeline(ctx, opts);
    const MapperFile src = ctx.source().add_source("test.src", "x := 1;\nf():Int32 := { y := 2; @return y; };\n");
    PipelineResult result = pipeline.run(RunRequest::analysis(), src);

    ASSERT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_TRUE(result.semantics.has_value()) << "semantic model must be produced by the Semantic step";
    EXPECT_EQ(result.semantics->mainModuleIndex().value, result.mainModuleIndex.value);

    bool sawLocal = false;
    for (const SymbolInfo& info : result.semantics->symbols()) {
        sawLocal = sawLocal || info.name == "$y";
    }
    EXPECT_TRUE(sawLocal) << "local declaration must be projected into the symbol index";
}

TEST(SemanticModel, LocalScopeSurvivesBlockExit) {
    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    PipelineOpts opts;
    Pipeline pipeline(ctx, opts);
    const MapperFile src = ctx.source().add_source("test.src", "f():Int32 := { y := 2; @return y; };\n");
    PipelineResult result = pipeline.run(RunRequest::analysis(), src);

    ASSERT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_TRUE(result.semantics.has_value());

    const ScopeStack& scopes = result.semantics->table().scopes();
    std::size_t closedScope = 0;
    const Symbol* closedScopeSymbol = nullptr;
    bool closedScopeWithY = false;
    for (std::size_t index = 0; index < scopes.size(); ++index) {
        const Scope& scope = scopes.node(index);
        if (!scope.closed) {
            continue;
        }
        for (const auto& [key, symbol] : scope.symbols) {
            if (key.name == "$y") {
                closedScopeWithY = true;
                closedScope = index;
                closedScopeSymbol = &symbol;
            }
        }
    }
    EXPECT_TRUE(closedScopeWithY) << "persistent ScopeTree must keep the closed function scope with locals";
    if (closedScopeWithY) {
        // Лексический поиск от закрытого скоупа работает после прогона.
        const Symbol* looked = result.semantics->lookupAt(closedScope, exportedKeyOf("$y"));
        ASSERT_NE(looked, nullptr);
        EXPECT_EQ(looked->name, "$y");
        EXPECT_TRUE(scopes.node(closedScope).closed);
        // Обратный путь: объявление → символ.
        ASSERT_NE(closedScopeSymbol, nullptr);
        EXPECT_EQ(result.semantics->symbolOf(closedScopeSymbol->decl), closedScopeSymbol);
    }
    // Позиция объявления функции `f` (валидный range создателя) попадает в закрытый скоуп.
    for (const SymbolInfo& info : result.semantics->symbols()) {
        if (info.name != "f" || info.nameRange.isInvalid()) {
            continue;
        }
        const std::size_t scope = result.semantics->scopeAt(info.nameRange);
        EXPECT_NE(scope, 0u);
        EXPECT_TRUE(scopes.node(scope).closed);
    }
}

} // namespace
} // namespace trust
