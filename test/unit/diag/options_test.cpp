#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "semantic/diag.hpp"
#include "transpiler/diag.hpp"
#include "diag/base_diags.hpp"

#include <gtest/gtest.h>

#include <sstream>

using namespace trust;

TEST(Options, DefaultValues) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Warning);
}

TEST(Options, CustomDefault) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::Format); // default Error
    EXPECT_EQ(opts.get(semantic::DiagId::Format), Severity::Error);
}

TEST(Options, SetAndGet) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    opts.set(semantic::DiagId::UnusedVariable, Severity::Fatal);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Fatal);
}

TEST(Options, SetAndGetByName) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    opts.setByName("unused-variable", Severity::Fatal);
    EXPECT_EQ(opts.getByName("unused-variable"), Severity::Fatal);
}

TEST(Options, SeverityFatal) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    opts.set(semantic::DiagId::UnusedVariable, Severity::Fatal);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Fatal);
    EXPECT_EQ(opts.getByName("unused-variable"), Severity::Fatal);
}

TEST(Options, SeverityIgnored) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    opts.set(semantic::DiagId::UnusedVariable, std::nullopt);
    EXPECT_FALSE(opts.get(semantic::DiagId::UnusedVariable).has_value());
    EXPECT_FALSE(opts.getByName("unused-variable").has_value());
}

TEST(Options, IsRegistered) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    EXPECT_TRUE(opts.is_registered(semantic::DiagId::UnusedVariable));
    EXPECT_TRUE(opts.isRegisteredByName("unused-variable"));
    EXPECT_FALSE(opts.isRegisteredByName("unknown"));
}

TEST(Options, DiagName) {
    EXPECT_EQ(diagName(semantic::DiagId::UnusedVariable), "unused-variable");
    EXPECT_EQ(diagName(semantic::DiagId::UnusedParameter), "unused-parameter");
    EXPECT_EQ(diagName(diag::DiagId::Deprecated), "deprecated");
    EXPECT_EQ(diagName(semantic::DiagId::WidenAny), "widen-any");
}

TEST(Options, UnusedParameterSeparateDiagnostic) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    opts.add(semantic::DiagId::UnusedParameter);
    opts.set(semantic::DiagId::UnusedVariable, std::nullopt);
    EXPECT_FALSE(opts.get(semantic::DiagId::UnusedVariable).has_value());
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedParameter), Severity::Warning); // не затронут
}

TEST(Options, PushPopSeverity) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    opts.set(semantic::DiagId::UnusedVariable, Severity::Fatal);
    opts.push();
    opts.set(semantic::DiagId::UnusedVariable, Severity::Error);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Error);
    opts.pop();
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Fatal);
}

TEST(Options, PushPopMultipleOptions) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    opts.add(diag::DiagId::Deprecated);
    opts.add(semantic::DiagId::UnusedParameter);
    opts.set(semantic::DiagId::UnusedVariable, Severity::Fatal);
    opts.set(diag::DiagId::Deprecated, Severity::Error);
    opts.set(semantic::DiagId::UnusedParameter, Severity::Fatal);

    opts.push();
    opts.set(semantic::DiagId::UnusedVariable, Severity::Warning);
    opts.set(diag::DiagId::Deprecated, Severity::Warning);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Warning);
    EXPECT_EQ(opts.get(diag::DiagId::Deprecated), Severity::Warning);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedParameter), Severity::Fatal); // не затронут

    opts.pop();
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Fatal);
    EXPECT_EQ(opts.get(diag::DiagId::Deprecated), Severity::Error);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedParameter), Severity::Fatal);
}

TEST(Options, WarnGroupFromCli) {
    EXPECT_EQ(Options::warnGroupFromCli("all"), WG_Wall);
    EXPECT_EQ(Options::warnGroupFromCli("extra"), WG_Wextra);
    EXPECT_EQ(Options::warnGroupFromCli("pedantic"), WG_Wpedantic);
    EXPECT_EQ(Options::warnGroupFromCli("unused"), WG_Wunused);
    EXPECT_EQ(Options::warnGroupFromCli("bogus"), WG_None);
}

TEST(Options, WarnGroupsMembership) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable); // Wall|Wextra|Wunused
    opts.add(semantic::DiagId::WidenAny);       // Wextra|Wconversion
    EXPECT_NE(static_cast<unsigned>(opts.warn_groups(semantic::DiagId::UnusedVariable)) & static_cast<unsigned>(WG_Wall), 0u);
    EXPECT_NE(static_cast<unsigned>(opts.warn_groups(semantic::DiagId::UnusedVariable)) & static_cast<unsigned>(WG_Wunused), 0u);
    EXPECT_EQ(static_cast<unsigned>(opts.warn_groups(semantic::DiagId::WidenAny)) & static_cast<unsigned>(WG_Wall), 0u);
}

TEST(Options, ParseArgv_WallEnablesAllToDefault) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    opts.add(diag::DiagId::Deprecated);
    char* argv[] = {const_cast<char*>("-Wunused-variable=ignore"), const_cast<char*>("-Wall")};
    opts.parse_argv(argv);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Warning);
    EXPECT_EQ(opts.get(diag::DiagId::Deprecated), Severity::Warning);
}

TEST(Options, ParseArgv_WextraEnablesAll) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    char* argv[] = {const_cast<char*>("-Wunused-variable=ignore"), const_cast<char*>("-Wextra")};
    opts.parse_argv(argv);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Warning);
}

TEST(Options, ParseArgv_WallOnlyAffectsWallGroup) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable); // in Wall
    opts.add(semantic::DiagId::WidenAny);       // NOT in Wall (Wextra|Wconversion)
    char* argv[] = {const_cast<char*>("-Wunused-variable=ignore"), const_cast<char*>("-Wwiden-any=ignore"), const_cast<char*>("-Wall")};
    opts.parse_argv(argv);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Warning);
    EXPECT_FALSE(opts.get(semantic::DiagId::WidenAny).has_value());
}

TEST(Options, ParseArgv_WunusedGroup) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable); // in Wunused
    opts.add(diag::DiagId::Deprecated);         // not in Wunused
    char* argv[] = {const_cast<char*>("-Wunused-variable=ignore"), const_cast<char*>("-Wdeprecated=ignore"), const_cast<char*>("-Wunused")};
    opts.parse_argv(argv);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Warning);
    EXPECT_FALSE(opts.get(diag::DiagId::Deprecated).has_value());
}

TEST(Options, ParseArgv_WnoGroupDisables) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable); // in Wunused
    char* argv[] = {const_cast<char*>("-Wno-unused")};
    opts.parse_argv(argv);
    EXPECT_FALSE(opts.get(semantic::DiagId::UnusedVariable).has_value());
}

TEST(Options, ParseArgvWnoSeverityDisables) {
    // `-Wno-<name>` выключает severity-диагностику (ignore), стиль clang/gcc.
    // Используются НЕ-групповые имена (embed/solver/sigil/unused-variable - не WarnGroup).
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable); // default Warning
    opts.add(semantic::DiagId::Embed);          // default Warning
    opts.add(semantic::DiagId::NoSigil);        // default Warning
    opts.add(semantic::DiagId::Solver);         // default Warning
    char* argv[] = {const_cast<char*>("-Wno-unused-variable"), const_cast<char*>("-Wno-embed"), const_cast<char*>("-Wno-sigil"),
                    const_cast<char*>("-Wno-solver")};
    opts.parse_argv(argv);
    EXPECT_FALSE(opts.get(semantic::DiagId::UnusedVariable).has_value());
    EXPECT_FALSE(opts.get(semantic::DiagId::Embed).has_value());
    EXPECT_FALSE(opts.get(semantic::DiagId::NoSigil).has_value());
    EXPECT_FALSE(opts.get(semantic::DiagId::Solver).has_value());
}

TEST(Options, ParseArgvWSeverityReenablesToDefault) {
    // `-W<name>` (без =) возвращает severity-диагностику к уровню по умолчанию (после =ignore),
    // стиль clang/gcc. `unused-variable` - не-групповое имя (группа - `unused`).
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable); // default Warning
    opts.add(semantic::DiagId::Format);         // default Error (группа `format`, обрабатывается отдельно)
    char* argv[] = {const_cast<char*>("-Wunused-variable=ignore"), const_cast<char*>("-Wunused-variable"), const_cast<char*>("-Wformat=ignore"),
                    const_cast<char*>("-Wformat")};
    opts.parse_argv(argv);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Warning);
    EXPECT_EQ(opts.get(semantic::DiagId::Format), Severity::Error);
}

TEST(Options, WerrorPromotesWarnings) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    char* argv[] = {const_cast<char*>("-Werror")};
    opts.parse_argv(argv);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Error);
    // -Wno-error возвращает обратно.
    char* argv2[] = {const_cast<char*>("-Wno-error")};
    opts.parse_argv(argv2);
    EXPECT_EQ(opts.get(semantic::DiagId::UnusedVariable), Severity::Warning);
}

TEST(Options, PrintHelpContainsDiagnostics) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add(semantic::DiagId::UnusedVariable);
    opts.add_flag(semantic::FlagKind::Lint);
    opts.add_flag(transpiler::FlagKind::Comments);
    std::ostringstream os;
    opts.printHelp(os);
    const std::string s = os.str();
    EXPECT_NE(s.find("-Wunused-variable"), std::string::npos);
    EXPECT_NE(s.find("-Whelp"), std::string::npos);
    EXPECT_NE(s.find("Diagnostics:"), std::string::npos);
    EXPECT_NE(s.find("Analysis flags:"), std::string::npos);
    EXPECT_NE(s.find("Codegen flags:"), std::string::npos);
    EXPECT_NE(s.find("Unused variable"), std::string::npos);
    EXPECT_NE(s.find("Lint analyzer"), std::string::npos);
}

TEST(Options, FlagComments) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add_flag(transpiler::FlagKind::Comments);
    EXPECT_FALSE(opts.is_enabled(transpiler::FlagKind::Comments));
    char a[] = "-Wcomments";
    char* argv[] = {a};
    auto rest = opts.parse_argv(argv);
    EXPECT_TRUE(opts.is_enabled(transpiler::FlagKind::Comments));
    EXPECT_TRUE(rest.empty());
    EXPECT_FALSE(opts.setEnabledByName("unknown-flag", true));
}

TEST(Options, FlagBacktrace) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add_flag(transpiler::FlagKind::Backtrace);
    EXPECT_FALSE(opts.is_enabled(transpiler::FlagKind::Backtrace));
    {
        char a[] = "-Wbacktrace";
        char* argv[] = {a};
        auto r = opts.parse_argv(argv);
        EXPECT_TRUE(opts.is_enabled(transpiler::FlagKind::Backtrace));
        EXPECT_TRUE(r.empty());
    }
    {
        char a[] = "-Wno-backtrace";
        char* argv[] = {a};
        auto r = opts.parse_argv(argv);
        EXPECT_FALSE(opts.is_enabled(transpiler::FlagKind::Backtrace));
        EXPECT_TRUE(r.empty());
    }
    EXPECT_TRUE(opts.setEnabledByName("backtrace", true));
    EXPECT_TRUE(opts.isEnabledByName("backtrace"));
}

TEST(Options, ParseArgvFlagNegate) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add_flag(transpiler::FlagKind::Comments);
    char a[] = "-Wno-comments";
    char* argv[] = {a};
    auto rest = opts.parse_argv(argv);
    EXPECT_FALSE(opts.is_enabled(transpiler::FlagKind::Comments));
    EXPECT_TRUE(rest.empty());
}

TEST(Options, ParseArgvFlagWithValue) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add_flag(semantic::FlagKind::Lint);
    char a[] = "-Wlint=aggressive";
    char* argv[] = {a};
    auto rest = opts.parse_argv(argv);
    EXPECT_TRUE(opts.is_enabled(semantic::FlagKind::Lint));
    ASSERT_TRUE(opts.flag_value(semantic::FlagKind::Lint).has_value());
    EXPECT_EQ(*opts.flag_value(semantic::FlagKind::Lint), "aggressive");
    EXPECT_TRUE(rest.empty());
}

TEST(Options, FlagValueSetReset) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add_flag(semantic::FlagKind::Lint);
    EXPECT_FALSE(opts.flag_value(semantic::FlagKind::Lint).has_value());
    opts.setFlagValueByName("lint", "strict");
    EXPECT_TRUE(opts.is_enabled(semantic::FlagKind::Lint));
    ASSERT_TRUE(opts.flag_value(semantic::FlagKind::Lint).has_value());
    EXPECT_EQ(*opts.flag_value(semantic::FlagKind::Lint), "strict");
    opts.setEnabledByName("lint", false);
    EXPECT_FALSE(opts.is_enabled(semantic::FlagKind::Lint));
    EXPECT_FALSE(opts.flag_value(semantic::FlagKind::Lint).has_value());
}

TEST(Options, FlagValuePushPop) {
    DiagnosticEngine diag;
    Options opts(diag);
    opts.add_flag(semantic::FlagKind::Lint);
    opts.setFlagValueByName("lint", "basic");
    opts.push();
    opts.setFlagValueByName("lint", "aggressive");
    EXPECT_EQ(*opts.flag_value(semantic::FlagKind::Lint), "aggressive");
    opts.pop();
    ASSERT_TRUE(opts.flag_value(semantic::FlagKind::Lint).has_value());
    EXPECT_EQ(*opts.flag_value(semantic::FlagKind::Lint), "basic");
    EXPECT_TRUE(opts.is_enabled(semantic::FlagKind::Lint));
}
