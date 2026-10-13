// test/unit/semantic/analysis_zones_test.cpp
// Зоны прогона семантики: NamespaceScope/ExprFacts/AnalysisState как самостоятельные
// владельцы (не через AnalysisContext-обёртки). Проверяется контракт пустого прогона и
// round-trip материализованного факта.
#include "semantic/core/pass.hpp"

#include "session/context.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class AnalysisZonesTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(AnalysisZonesTest, NamespaceScopeReportsEmptyStack) {
    AnalysisContext actx(m_ctx);
    const NamespaceScope& namespaces = actx.namespaces();
    EXPECT_TRUE(namespaces.namespacePath().empty());
    EXPECT_EQ(namespaces.namespaceFull(), "::");
    EXPECT_EQ(namespaces.currentFunc(), nullptr);
    EXPECT_EQ(namespaces.currentClass(), nullptr);
    EXPECT_FALSE(namespaces.insideTypeBody());
    EXPECT_FALSE(namespaces.isInLocalScope());
    EXPECT_FALSE(namespaces.isInLoop());
}

TEST_F(AnalysisZonesTest, AnalysisStateCountersAreStrictlyIncreasing) {
    AnalysisContext actx(m_ctx);
    AnalysisState& state = actx.state();
    EXPECT_LT(state.nextMatchTempId(), state.nextMatchTempId());
    EXPECT_LT(state.nextResultTempId(), state.nextResultTempId());
    EXPECT_FALSE(state.isModuleBodyAnalyzed(ModuleIndex::invalid()));
    state.markModuleBodyAnalyzed(ModuleIndex::invalid());
    EXPECT_TRUE(state.isModuleBodyAnalyzed(ModuleIndex::invalid()));
}

TEST_F(AnalysisZonesTest, ExprFactsRoundTripsStateInNodeField) {
    AnalysisContext actx(m_ctx);
    VarDecl var;
    const TypeId int32 = m_ctx.readTypes().getType(type::Int32);
    actx.facts().setExprState(&var, QualType::fromTypeId(int32).withInferred());
    const QualType state = actx.facts().exprState(var);
    EXPECT_EQ(state.getTypeId(), int32);
    EXPECT_TRUE(state.isInferred());
    EXPECT_EQ(actx.facts().exprType(var), int32);
}

} // namespace
} // namespace trust
