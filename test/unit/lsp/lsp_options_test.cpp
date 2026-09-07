// test/unit/lsp/lsp_options_test.cpp
// Юнит-тесты LSP-специфичной обработки опций анализа (lsp/lsp_options.hpp):
//   - extractShebangOptions - извлечение опций компилятора из строки шебанга файла;
//   - applyAnalysisArgsBySource - последовательное применение опций окружения и шебанга
//     по приоритету shebangMode с атрибуцией ошибок по источнику.

#include "lsp/lsp_options.hpp"
#include "diag/context.hpp"
#include "semantic/solver.hpp"
#include "gtest/gtest.h"

#include <functional>
#include <string>
#include <vector>

namespace trust {
namespace lsp {
namespace {

TEST(ExtractShebangOptions, Basic) {
    const std::vector<std::string> got = extractShebangOptions("#!trust --run -Wsigil=ignore --solver-mode=assert\n%add(x:Int32):Int32\n");
    const std::vector<std::string> want = {"--run", "-Wsigil=ignore", "--solver-mode=assert"};
    EXPECT_EQ(got, want);
}

TEST(ExtractShebangOptions, SpaceSeparatedValue) {
    const std::vector<std::string> got = extractShebangOptions("#!trust --solver-mode assert\nrest");
    const std::vector<std::string> want = {"--solver-mode", "assert"};
    EXPECT_EQ(got, want);
}

TEST(ExtractShebangOptions, NoShebang) {
    EXPECT_TRUE(extractShebangOptions("%add(x:Int32):Int32\n").empty());
    EXPECT_TRUE(extractShebangOptions("").empty());
    EXPECT_TRUE(extractShebangOptions("x").empty());
}

TEST(ExtractShebangOptions, ShebangWithoutOptions) {
    const std::vector<std::string> got = extractShebangOptions("#!trust\nbody");
    const std::vector<std::string> want = {};
    EXPECT_EQ(got, want);
}

TEST(ExtractShebangOptions, CarriageReturnTrimmed) {
    const std::vector<std::string> got = extractShebangOptions("#!trust --solver-mode=assert\r\nbody");
    const std::vector<std::string> want = {"--solver-mode=assert"};
    EXPECT_EQ(got, want);
}

// Вспомогательный: применяет и собирает ошибки (msg, fromShebang) по источнику.
struct ApplyResult {
    std::vector<std::pair<std::string, bool>> errors;
};

// Применяет опции и возвращает собранные ошибки.
static ApplyResult apply(trust::Options& opts, const std::vector<std::string>& env, const std::vector<std::string>& shebang, ShebangMode mode) {
    ApplyResult r;
    applyAnalysisArgsBySource(opts, env, shebang, mode, [&](const std::string& msg, bool fromShebang) { r.errors.emplace_back(msg, fromShebang); });
    return r;
}

TEST(ApplyBySource, EnvAfterShebangEnvOverrides) {
    trust::Context ctx;
    const std::vector<std::string> env = {"--solver-mode=export"};
    const std::vector<std::string> shebang = {"--solver-mode=assert"};
    const ApplyResult r = apply(ctx.opts(), env, shebang, ShebangMode::EnvAfterShebang);
    EXPECT_TRUE(r.errors.empty());
    // shebang сначала, env после -> env (export) сильнее.
    EXPECT_EQ(semantic::solverModeFromOptions(ctx.opts()), semantic::SolverMode::kExport);
}

TEST(ApplyBySource, EnvBeforeShebangShebangOverrides) {
    trust::Context ctx;
    const std::vector<std::string> env = {"--solver-mode=export"};
    const std::vector<std::string> shebang = {"--solver-mode=assert"};
    const ApplyResult r = apply(ctx.opts(), env, shebang, ShebangMode::EnvBeforeShebang);
    EXPECT_TRUE(r.errors.empty());
    // env сначала, shebang после -> shebang (assert) сильнее.
    EXPECT_EQ(semantic::solverModeFromOptions(ctx.opts()), semantic::SolverMode::kAssert);
}

TEST(ApplyBySource, IgnoreUsesEnvOnly) {
    trust::Context ctx;
    const std::vector<std::string> env = {"--solver-mode=calculate"};
    const std::vector<std::string> shebang = {"--solver-mode=assert"};
    const ApplyResult r = apply(ctx.opts(), env, shebang, ShebangMode::Ignore);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_EQ(semantic::solverModeFromOptions(ctx.opts()), semantic::SolverMode::kCalculate);
}

TEST(ApplyBySource, ShebangOnlyUsesShebang) {
    trust::Context ctx;
    const std::vector<std::string> env = {"--solver-mode=calculate"};
    const std::vector<std::string> shebang = {"--solver-mode=assert"};
    const ApplyResult r = apply(ctx.opts(), env, shebang, ShebangMode::ShebangOnly);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_EQ(semantic::solverModeFromOptions(ctx.opts()), semantic::SolverMode::kAssert);
}

TEST(ApplyBySource, ShebangErrorAttributedToShebang) {
    trust::Context ctx;
    const std::vector<std::string> env = {"--solver-mode=assert"};
    const std::vector<std::string> shebang = {"--solver-mode=bogus"};
    const ApplyResult r = apply(ctx.opts(), env, shebang, ShebangMode::EnvAfterShebang);
    // Ошибка шебанга (bogus) атрибутирована fromShebang=true; env (assert) применяется.
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_TRUE(r.errors[0].second); // fromShebang
    EXPECT_NE(r.errors[0].first.find("solver-mode"), std::string::npos);
    EXPECT_EQ(semantic::solverModeFromOptions(ctx.opts()), semantic::SolverMode::kAssert);
}

TEST(ApplyBySource, EnvErrorAttributedToEnv) {
    trust::Context ctx;
    const std::vector<std::string> env = {"--solver-mode=bogus"};
    const std::vector<std::string> shebang = {"--solver-mode=assert"};
    const ApplyResult r = apply(ctx.opts(), env, shebang, ShebangMode::EnvAfterShebang);
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_FALSE(r.errors[0].second); // from env
    EXPECT_NE(r.errors[0].first.find("solver-mode"), std::string::npos);
}

} // namespace
} // namespace lsp
} // namespace trust
