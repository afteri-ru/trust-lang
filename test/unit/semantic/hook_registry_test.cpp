// test/unit/semantic/hook_registry_test.cpp
// Контракт реестра семантических анализаторов: состав и ПОРЯДОК подключения хуков.
// Порядок = порядок колбэков ядра (NameResolutionPass дописывает хук в конец), поэтому
// он проверяется как данные через SemanticPassRunner::activeHooks().
#include "semantic/core/pass_runner.hpp"
#include "semantic/diag.hpp"
#include "diag/options.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"
#include "utils/utils.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace {

class HookRegistryTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    void TearDown() override { setErrs(m_prev_err); }

    /// Прогон семантики на пустом AST: интересует только состав реестра хуков.
    std::vector<std::string> activeHooks() {
        SemanticPassRunner runner(m_ctx, *m_types);
        std::vector<AstNodePtr> nodes;
        (void)runner.run(nodes);
        std::vector<std::string> names;
        for (const std::string_view name : runner.activeHooks()) {
            names.emplace_back(name);
        }
        return names;
    }

    void enableStackCheck(std::string_view mode) {
        m_ctx.opts().set_enabled(semantic::FlagKind::StackCheck, true);
        m_ctx.opts().set_flag_value(semantic::FlagKind::StackCheck, mode);
    }

    std::ostream* m_prev_err = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(HookRegistryTest, DefaultHasOnlyAlwaysOnHooks) {
    const std::vector<std::string> expected{"context-macro-expander", "symbol-collector", "native-ref", "ref-cycle"};
    EXPECT_EQ(activeHooks(), expected);
}

TEST_F(HookRegistryTest, LintFollowsContextMacroExpander) {
    m_ctx.opts().set_enabled(semantic::FlagKind::Lint, true);
    const std::vector<std::string> expected{"context-macro-expander", "lint", "symbol-collector", "native-ref", "ref-cycle"};
    EXPECT_EQ(activeHooks(), expected);
}

TEST_F(HookRegistryTest, StackCheckInferDisabledForExplicitMode) {
    enableStackCheck("explicit");
    const std::vector<std::string> expected{"context-macro-expander", "symbol-collector", "native-ref", "ref-cycle"};
    EXPECT_EQ(activeHooks(), expected);
}

TEST_F(HookRegistryTest, StackCheckInferEnabledForRecursiveModes) {
    enableStackCheck("recursion");
    const std::vector<std::string> expected{"context-macro-expander", "stack-check-infer", "symbol-collector", "native-ref", "ref-cycle"};
    EXPECT_EQ(activeHooks(), expected);
}

TEST_F(HookRegistryTest, FullOrderIsContract) {
    m_ctx.opts().set_enabled(semantic::FlagKind::Lint, true);
    enableStackCheck("auto");
    const std::vector<std::string> expected{"context-macro-expander", "lint", "stack-check-infer", "symbol-collector", "native-ref", "ref-cycle"};
    EXPECT_EQ(activeHooks(), expected);
}

} // namespace
} // namespace trust
