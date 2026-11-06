// test/unit/semantic/semantic_model_test.cpp
// Модель семантики — продукт шага Semantic: строится без флагов и сохраняет ЛОКАЛЬНЫЕ
// объявления в persistent-дереве скоупов (после выхода из блока).
#include "pipeline/pipeline.hpp"
#include "semantic/core/pass_runner.hpp"
#include "semantic/semantic_model.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"
#include "gtest/gtest.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

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

// Снимок реестра (`SemanticModel`) действителен, пока реестр не сброшен: `TypeSystem::reset()`
// — единственная точка инвалидации (R7), после неё обращение к типовой системе снимка обязано
// дать диагностику, а не молчаливые устаревшие данные.
TEST(SemanticModel, StaleSnapshotAfterRegistryResetIsDiagnosed) {
    Context ctx;
    TypeSystem reg(ctx.diag(), ctx.opts());
    ctx.setTypes(&reg);

    SemanticPassRunner runner(ctx, reg);
    std::vector<AstNodePtr> empty;
    ASSERT_TRUE(runner.run(empty));
    auto model = std::make_shared<SemanticModel>(runner.takeAnalysis(), ctx.stringsPool(), std::shared_ptr<const TypeSystem>(&reg, [](const TypeSystem*) {}),
                                                 ModuleProjection{}, ReferenceIndex{});
    EXPECT_NO_THROW((void)model->typeSystem());

    reg.reset();
    EXPECT_ANY_THROW((void)model->typeSystem());
}

// Один Pipeline — одна компиляция: повторный run() запрещён, иначе per-run владельцы
// (TargetContract/SourceMapWriter/ModuleLoader) смешаются с уже сброшенным реестром типов.
TEST(SemanticModel, PipelineRejectsSecondRun) {
    Context ctx;
    PipelineOpts opts;
    Pipeline pipeline(ctx, opts);
    const MapperFile src1 = ctx.source().add_source("a.src", "x := 1;\n");
    EXPECT_TRUE(pipeline.run(RunRequest::analysis(), src1).isValid());

    const MapperFile src2 = ctx.source().add_source("b.src", "y := 2;\n");
    EXPECT_ANY_THROW((void)pipeline.run(RunRequest::analysis(), src2));
}

} // namespace
} // namespace trust
