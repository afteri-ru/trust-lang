// test/unit/pipeline/analysis_options_test.cpp
// Юнит-тесты единой точки применения опций анализа (pipeline/analysis_options.hpp):
//   - commonAnalysisOptions - единый источник общих опций (--solver-mode/--keywords/-f...-unroll);
//   - applyAnalysisArgs - применение смеси `-W...` и поведенческих флагов
//     (--solver-mode, --keywords, -fsolver-loop-unroll) к diag::Options, независимо от
//     порядка (в отличие от applyDiagnostics, останавливающегося на первом не -W);
//   - возврат ошибок разбора/применения (unknown option, missing value, невалидное значение,
//     неизвестная -W-опция) - для отчёта LSP диагностикой.
// Извлечение опций шебанга вынесено в lsp/lsp_options_test.cpp (LSP-специфично).

#include "pipeline/analysis_options.hpp"
#include "pipeline/cli.hpp"
#include "diag/context.hpp"
#include "semantic/diag.hpp"
#include "semantic/solver.hpp"
#include "gtest/gtest.h"

#include <string>
#include <vector>

namespace trust {
namespace {

TEST(CommonAnalysisOptions, NamesAndArity) {
    const std::vector<DriverOption> opts = commonAnalysisOptions(101, 102, 103);
    ASSERT_EQ(opts.size(), 3u);
    EXPECT_EQ(opts[0].name, "solver-mode");
    EXPECT_EQ(opts[0].id, 101);
    EXPECT_EQ(opts[0].kind, CliOpt::Value);
    EXPECT_EQ(opts[1].name, "keywords");
    EXPECT_EQ(opts[1].id, 102);
    EXPECT_EQ(opts[1].kind, CliOpt::Value);
    EXPECT_EQ(opts[2].name, "solver-loop-unroll");
    EXPECT_EQ(opts[2].id, 103);
    EXPECT_EQ(opts[2].kind, CliOpt::Flag);
}

// Применение смеси опций к Context-опциям (единая точка: applyAnalysisArgs).
TEST(AnalysisOptions, SolverModeAndSigilOrderIndependent) {
    Context ctx;
    // Порядок не важен: applyAnalysisArgs разбирает и -W, и поведенческие флаги.
    const std::vector<std::string> args = {"-Wsigil=ignore", "--solver-mode=assert"};
    EXPECT_EQ(applyAnalysisArgs(ctx.opts(), args), "");

    EXPECT_EQ(semantic::solverModeFromOptions(ctx.opts()), semantic::SolverMode::kAssert);
    EXPECT_TRUE(semantic::solverAssertEnabled(ctx.opts()));
    // -Wsigil=ignore: NoSigil выключен (nullopt).
    EXPECT_FALSE(ctx.opts().get(semantic::DiagId::NoSigil).has_value());
}

TEST(AnalysisOptions, KeywordsAndSolverLoopUnroll) {
    Context ctx;
    const std::vector<std::string> args = {"--keywords=trust_pre,trust_post", "-fsolver-loop-unroll"};
    EXPECT_EQ(applyAnalysisArgs(ctx.opts(), args), "");

    EXPECT_EQ(ctx.opts().flagValueByName("keywords"), std::string_view("trust_pre,trust_post"));
    EXPECT_TRUE(ctx.opts().isEnabledByName("solver-loop-unroll"));
}

TEST(AnalysisOptions, ExecutionOptionsAreIgnored) {
    Context ctx;
    // Опции исполнения/линковки не являются опциями анализа и не должны ломаться/применяться.
    const std::vector<std::string> args = {"--run", "-o", "out", "--solver-mode=assert"};
    EXPECT_EQ(applyAnalysisArgs(ctx.opts(), args), "");

    EXPECT_EQ(semantic::solverModeFromOptions(ctx.opts()), semantic::SolverMode::kAssert);
    // Отсутствие поведения при пустом списке.
    Context empty_ctx;
    applyAnalysisArgs(empty_ctx.opts(), {});
    EXPECT_FALSE(semantic::solverModeFromOptions(empty_ctx.opts()).has_value());
}

TEST(AnalysisOptions, EmptyArgsNoEffect) {
    Context ctx;
    const std::vector<std::string> args;
    EXPECT_EQ(applyAnalysisArgs(ctx.opts(), args), "");
    EXPECT_FALSE(semantic::solverModeFromOptions(ctx.opts()).has_value());
    EXPECT_TRUE(ctx.opts().get(semantic::DiagId::Solver).has_value()); // -Wsolver остаётся default
}

TEST(AnalysisOptions, UnknownOptionReturnsError) {
    Context ctx;
    const std::string err = applyAnalysisArgs(ctx.opts(), std::vector<std::string>{"--bogus-option"});
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("bogus-option"), std::string::npos);
}

TEST(AnalysisOptions, MissingValueReturnsError) {
    Context ctx;
    // `--solver-mode` без значения: arity-aware парсер сообщает об ошибке.
    const std::string err = applyAnalysisArgs(ctx.opts(), std::vector<std::string>{"--solver-mode"});
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("solver-mode"), std::string::npos);
}

TEST(AnalysisOptions, InvalidSolverModeValueReturnsError) {
    Context ctx;
    // Невалидное значение - не тихий пропуск: applyBehavioralFlags сообщает об ошибке,
    // режим НЕ применяется.
    const std::string err = applyAnalysisArgs(ctx.opts(), std::vector<std::string>{"--solver-mode=bogus"});
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("solver-mode"), std::string::npos);
    EXPECT_FALSE(semantic::solverModeFromOptions(ctx.opts()).has_value());
}

TEST(AnalysisOptions, UnknownWOptionReturnsError) {
    Context ctx;
    // Неизвестная -W-опция: Options::parse_argv бросает invalid_argument -> перехватывается.
    const std::string err = applyAnalysisArgs(ctx.opts(), std::vector<std::string>{"-Wbogus-warning"});
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("bogus-warning"), std::string::npos);
}

} // namespace
} // namespace trust
