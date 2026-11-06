#include "pipeline/backend/stages.hpp"
#include "pipeline/pipeline.hpp"

#include "analysis/modes.hpp"
#include "module_loader/module_loader.hpp"
#include "session/context.hpp"
#include "target/target.hpp"
#include "types/type_system.hpp"

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace trust {
namespace {

using backend::BackendRequest;
using backend::BackendServices;
using backend::BackendStage;
using backend::StageOutcome;
using backend::StageOutputs;

// R12: стадия получает read-model порт прогона, а не мутабельный агрегат `Context`.
static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::declval<BackendServices>().services)>, RunServices>,
              "BackendServices must expose the run read-model port (R12)");
static_assert(!std::is_constructible_v<BackendServices, Context&, const Options&, ModuleLoader&>,
              "BackendServices must not be constructible from the mutable Context (R12)");

std::vector<std::string> g_calls;

bool requestAlways(const BackendRequest&) noexcept {
    return true;
}

bool requestNever(const BackendRequest&) noexcept {
    return false;
}

StageOutcome runA(BackendServices&, const BackendRequest&, StageOutputs&) {
    g_calls.emplace_back("a");
    return StageOutcome::Done;
}

StageOutcome runB(BackendServices&, const BackendRequest&, StageOutputs&) {
    g_calls.emplace_back("b");
    return StageOutcome::Done;
}

StageOutcome runNoWork(BackendServices&, const BackendRequest&, StageOutputs&) {
    g_calls.emplace_back("nowork");
    return StageOutcome::NoWork;
}

StageOutcome runError(BackendServices&, const BackendRequest&, StageOutputs&) {
    g_calls.emplace_back("error");
    return StageOutcome::Error;
}

class BackendStageTest : public ::testing::Test {
  protected:
    void SetUp() override {
        g_calls.clear();
        m_ctx.setTypes(&m_types);
        m_ctx.setTarget(&m_target);
        m_loader = std::make_unique<ModuleLoader>(m_ctx);
        m_opts.allow_semantic_on_errors = true;
    }

    BackendServices services() { return BackendServices{m_ctx.runServices(), m_opts, *m_loader}; }

    Context m_ctx;
    TypeSystem m_types{m_ctx.diag(), m_ctx.opts()};
    TargetContract m_target;
    PipelineOpts m_opts;
    std::unique_ptr<ModuleLoader> m_loader;
    HirModule m_hir{std::vector<AstNodePtr>{}};
    flow::MirModule m_mir;
    analysis::BehavioralModes m_modes;
};

TEST_F(BackendStageTest, RunsRequestedStagesInTableOrder) {
    const BackendStage stages[] = {
        {false, false, true, requestAlways, runA},
        {false, false, true, requestAlways, runB},
    };
    BackendServices svc = services();
    StageOutputs out;
    const BackendRequest req{&m_hir, &m_mir, m_modes};
    EXPECT_TRUE(backend::runBackendStages(stages, svc, req, out));
    EXPECT_EQ(g_calls, (std::vector<std::string>{"a", "b"}));
}

TEST_F(BackendStageTest, SkipsUnrequestedStage) {
    const BackendStage stages[] = {
        {false, false, true, requestNever, runA},
        {false, false, true, requestAlways, runB},
    };
    BackendServices svc = services();
    StageOutputs out;
    const BackendRequest req{&m_hir, &m_mir, m_modes};
    EXPECT_TRUE(backend::runBackendStages(stages, svc, req, out));
    EXPECT_EQ(g_calls, (std::vector<std::string>{"b"}));
}

TEST_F(BackendStageTest, NoWorkDoesNotStopPipeline) {
    const BackendStage stages[] = {
        {false, false, true, requestAlways, runNoWork},
        {false, false, true, requestAlways, runB},
    };
    BackendServices svc = services();
    StageOutputs out;
    const BackendRequest req{&m_hir, &m_mir, m_modes};
    EXPECT_TRUE(backend::runBackendStages(stages, svc, req, out));
    EXPECT_EQ(g_calls, (std::vector<std::string>{"nowork", "b"}));
}

TEST_F(BackendStageTest, ErrorStopsWhenPolicyRequires) {
    const BackendStage stages[] = {
        {false, false, true, requestAlways, runError},
        {false, false, true, requestAlways, runB},
    };
    BackendServices svc = services();
    StageOutputs out;
    const BackendRequest req{&m_hir, &m_mir, m_modes};
    EXPECT_FALSE(backend::runBackendStages(stages, svc, req, out));
    EXPECT_EQ(g_calls, (std::vector<std::string>{"error"}));
}

TEST_F(BackendStageTest, ErrorWithoutStopContinues) {
    const BackendStage stages[] = {
        {false, false, false, requestAlways, runError},
        {false, false, true, requestAlways, runB},
    };
    BackendServices svc = services();
    StageOutputs out;
    const BackendRequest req{&m_hir, &m_mir, m_modes};
    EXPECT_TRUE(backend::runBackendStages(stages, svc, req, out));
    EXPECT_EQ(g_calls, (std::vector<std::string>{"error", "b"}));
}

// Представление не построено (прогон без семантики/после ICE): стадия с needsHir не запускается,
// оркестратор сообщает внутреннюю ошибку.
TEST_F(BackendStageTest, MissingPrerequisiteReportsErrorAndStops) {
    const BackendStage stages[] = {
        {true, false, true, requestAlways, runA},
    };
    BackendServices svc = services();
    StageOutputs out;
    const BackendRequest req{nullptr, nullptr, m_modes}; // HIR не построен
    EXPECT_FALSE(backend::runBackendStages(stages, svc, req, out));
    EXPECT_TRUE(g_calls.empty());
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST(BackendStageMetadataTest, NamesAreDerivedFromKind) {
    EXPECT_EQ(backend::backendStageName(backend::BackendKind::Solver), "solver");
    EXPECT_EQ(backend::backendStageName(backend::BackendKind::Cpp), "cpp");
    EXPECT_EQ(backend::backendStageName(backend::BackendKind::Noop), "noop");
    EXPECT_EQ(backend::kBackendStages.size(), static_cast<std::size_t>(backend::BackendKind::kCount));
}

TEST(BackendStageMetadataTest, CombinedSolverAndCppAreBothRequested) {
    HirModule hir{std::vector<AstNodePtr>{}};
    flow::MirModule mir;
    analysis::BehavioralModes modes;
    BackendRequest req{&hir, &mir, modes};
    req.smt.emplace();
    req.cpp.emplace();
    EXPECT_TRUE(backend::solverStageRequested(req));
    EXPECT_TRUE(backend::cppStageRequested(req));
}

// Комбинированный прогон: solver нечего доказывать (контрактов нет) -> NoWork НЕ подавляет
// C++-кодогенерацию. Это контракт стадий, а не отдельный CLI-режим.
TEST(BackendStageIntegrationTest, EmptySolverWorkDoesNotSuppressCpp) {
    Context ctx;
    PipelineOpts opts;
    MapperFile source = ctx.source().add_source("combined.src", "@main() := { };\n", true);
    Pipeline pipeline(ctx, opts);

    MapperFile cppOut = ctx.source().add_output("combined.cppt");
    RunRequest req;
    req.smt.emplace();
    req.cpp.emplace();
    req.cpp->index = cppOut;

    PipelineResult result = pipeline.run(req, source);
    EXPECT_EQ(ctx.diag().errorCount(), 0);
    ASSERT_TRUE(result.smt.has_value());
    EXPECT_TRUE(result.smt->script.commands.empty());
    ASSERT_TRUE(result.cpp.has_value());
    EXPECT_FALSE(result.cpp->index.isInvalid());
}

} // namespace
} // namespace trust
