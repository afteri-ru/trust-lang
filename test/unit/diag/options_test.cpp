#include "diag/options.hpp"

#include <gtest/gtest.h>

using namespace trust;

TEST(Options, DefaultValues) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
}

TEST(Options, CustomDefault) {
    Options opts;
    opts.add_option(OptKind::UnusedVar, Severity::Error);
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Error);
}

TEST(Options, SetAndGet) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.set(OptKind::UnusedVar, Severity::Fatal);
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Fatal);
}

TEST(Options, SetAndGetByName) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.set("unused-var", Severity::Fatal);
    EXPECT_EQ(opts.get("unused-var"), Severity::Fatal);
}

TEST(Options, SeverityFatal) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.set(OptKind::UnusedVar, Severity::Fatal);
    EXPECT_EQ(opts.severity(OptKind::UnusedVar), Severity::Fatal);
    EXPECT_EQ(opts.severity("unused-var"), Severity::Fatal);
}

TEST(Options, SeverityError) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.set(OptKind::UnusedVar, Severity::Error);
    EXPECT_EQ(opts.severity(OptKind::UnusedVar), Severity::Error);
    EXPECT_EQ(opts.severity("unused-var"), Severity::Error);
}

TEST(Options, SeverityWarning) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.set(OptKind::UnusedVar, Severity::Warning);
    EXPECT_EQ(opts.severity(OptKind::UnusedVar), Severity::Warning);
    EXPECT_EQ(opts.severity("unused-var"), Severity::Warning);
}

TEST(Options, SeverityIgnored) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.set(OptKind::UnusedVar, std::nullopt);
    EXPECT_FALSE(opts.severity(OptKind::UnusedVar).has_value());
    EXPECT_FALSE(opts.severity("unused-var").has_value());
}

TEST(Options, IsRegistered) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    EXPECT_TRUE(opts.is_registered(OptKind::UnusedVar));
    EXPECT_TRUE(opts.is_registered("unused-var"));
    EXPECT_FALSE(opts.is_registered("unknown"));
}

TEST(Options, SetIgnored) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.set(OptKind::UnusedVar, std::nullopt);
    EXPECT_FALSE(opts.severity(OptKind::UnusedVar).has_value());
}

TEST(Options, OptName) {
    EXPECT_EQ(OptName(OptKind::UnusedVar), "unused-var");
    EXPECT_EQ(OptName(OptKind::Deprecated), "deprecated");
    EXPECT_EQ(OptName(OptKind::All), "all");
}

TEST(Options, ParseArgv_Simple) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    char* argv[] = {const_cast<char*>("-Wunused-var")};
    auto remaining = opts.parse_argv(argv);
    EXPECT_TRUE(remaining.empty());
}

TEST(Options, ParseArgv_Fatal) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    char* argv[] = {const_cast<char*>("-Wunused-var=fatal")};
    auto remaining = opts.parse_argv(argv);
    EXPECT_TRUE(remaining.empty());
    EXPECT_EQ(opts.severity("unused-var"), Severity::Fatal);
}

TEST(Options, ParseArgv_Error) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    char* argv[] = {const_cast<char*>("-Wunused-var=error")};
    auto remaining = opts.parse_argv(argv);
    EXPECT_TRUE(remaining.empty());
    EXPECT_EQ(opts.severity("unused-var"), Severity::Error);
}

TEST(Options, ParseArgv_Warning) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    char* argv[] = {const_cast<char*>("-Wunused-var=warning")};
    auto remaining = opts.parse_argv(argv);
    EXPECT_TRUE(remaining.empty());
    EXPECT_EQ(opts.severity("unused-var"), Severity::Warning);
}

TEST(Options, ParseArgv_Ignore) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    char* argv[] = {const_cast<char*>("-Wunused-var=ignore")};
    auto remaining = opts.parse_argv(argv);
    EXPECT_TRUE(remaining.empty());
    EXPECT_FALSE(opts.severity("unused-var").has_value());
}

TEST(Options, ParseArgv_Multiple) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.add_option(OptKind::Deprecated);
    char* argv[] = {
        const_cast<char*>("-Wunused-var=error"),
        const_cast<char*>("-Wdeprecated=ignore")
    };
    auto remaining = opts.parse_argv(argv);
    EXPECT_TRUE(remaining.empty());
    EXPECT_EQ(opts.severity("unused-var"), Severity::Error);
    EXPECT_FALSE(opts.severity("deprecated").has_value());
}

TEST(Options, ParseArgv_PositionalStopped) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    char* argv[] = {const_cast<char*>("prog"), const_cast<char*>("-Wunused-var"),
                    const_cast<char*>("file.cpp")};
    std::span<char*> args(argv);
    auto remaining = opts.parse_argv(args.subspan(1));
    ASSERT_EQ(remaining.size(), 1);
    EXPECT_STREQ(remaining[0], "file.cpp");
}

TEST(Options, ParseArgv_UnknownOptionThrows) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    char* argv[] = {const_cast<char*>("-Wunknown")};
    EXPECT_THROW(opts.parse_argv(argv), std::invalid_argument);
}

TEST(Options, ParseArgv_InvalidStatusThrows) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    char* argv[] = {const_cast<char*>("-Wunused-var=invalid")};
    EXPECT_THROW(opts.parse_argv(argv), std::invalid_argument);
}

TEST(Options, PushPop) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Error);
    EXPECT_EQ(opts.severity(OptKind::UnusedVar), Severity::Error);
    opts.pop();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
}

TEST(Options, PushPop_MultipleLevels) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Error);
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Fatal);
    EXPECT_EQ(opts.severity(OptKind::UnusedVar), Severity::Fatal);
    opts.pop();
    EXPECT_EQ(opts.severity(OptKind::UnusedVar), Severity::Error);
    opts.pop();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
}

TEST(Options, PushPop_MultipleOptions) {
    auto opts = Options::make({
        {OptKind::UnusedVar, Severity::Warning},
        {OptKind::Deprecated, Severity::Error},
        {OptKind::All, Severity::Fatal},
    });
    
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Error);
    opts.set(OptKind::Deprecated, Severity::Warning);
    
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Error);
    EXPECT_EQ(opts.get(OptKind::Deprecated), Severity::Warning);
    EXPECT_EQ(opts.get(OptKind::All), Severity::Fatal); // не изменялась
    
    opts.pop();
    
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
    EXPECT_EQ(opts.get(OptKind::Deprecated), Severity::Error);
    EXPECT_EQ(opts.get(OptKind::All), Severity::Fatal); // не изменялась
}

TEST(Options, PushPop_ThreeLevelsIndependent) {
    auto opts = Options::make({
        {OptKind::UnusedVar, Severity::Warning},
    });
    
    // Уровень 1: Error
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Error);
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Error);
    
    // Уровень 2: Fatal
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Fatal);
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Fatal);
    
    // Уровень 3: Remark
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Remark);
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Remark);
    
    // Возврат на уровень 2
    opts.pop();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Fatal);
    
    // Возврат на уровень 1
    opts.pop();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Error);
    
    // Возврат к исходному
    opts.pop();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
}

TEST(Options, PushPop_NoSetThenPop) {
    auto opts = Options::make({
        {OptKind::UnusedVar, Severity::Warning},
    });
    
    opts.push();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
    opts.pop();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
}

TEST(Options, PushPop_SetMultipleTimesInOneLevel) {
    auto opts = Options::make({
        {OptKind::UnusedVar, Severity::Warning},
    });
    
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Error);
    opts.set(OptKind::UnusedVar, Severity::Fatal);
    opts.set(OptKind::UnusedVar, Severity::Remark);
    
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Remark);
    
    opts.pop();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
}

TEST(Options, PushPop_NestedWithPartialSet) {
    auto opts = Options::make({
        {OptKind::UnusedVar, Severity::Warning},
        {OptKind::Deprecated, Severity::Error},
        {OptKind::All, Severity::Remark},
    });
    
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Fatal);
    // Deprecated не трогаем
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Fatal);
    EXPECT_EQ(opts.get(OptKind::Deprecated), Severity::Error);
    
    opts.push();
    opts.set(OptKind::Deprecated, Severity::Warning);
    // UnusedVar не трогаем
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Fatal);
    EXPECT_EQ(opts.get(OptKind::Deprecated), Severity::Warning);
    
    opts.pop();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Fatal);
    EXPECT_EQ(opts.get(OptKind::Deprecated), Severity::Error);
    
    opts.pop();
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
    EXPECT_EQ(opts.get(OptKind::Deprecated), Severity::Error);
}

TEST(Options, PopEmptyThrows) {
    Options opts;
    EXPECT_THROW(opts.pop(), std::runtime_error);
}

TEST(Options, MakeInitializer) {
    auto opts = Options::make({
        {OptKind::UnusedVar, Severity::Warning},
        {OptKind::Deprecated, Severity::Error},
        {OptKind::All, Severity::Fatal},
    });
    EXPECT_EQ(opts.get(OptKind::UnusedVar), Severity::Warning);
    EXPECT_EQ(opts.get(OptKind::Deprecated), Severity::Error);
    EXPECT_EQ(opts.severity(OptKind::All), Severity::Fatal);
}

TEST(Options, MakeInitializer_PushPop) {
    auto opts = Options::make({
        {OptKind::UnusedVar, Severity::Error},
    });
    opts.push();
    opts.set(OptKind::UnusedVar, Severity::Fatal);
    EXPECT_EQ(opts.severity(OptKind::UnusedVar), Severity::Fatal);
    opts.pop();
    EXPECT_EQ(opts.severity(OptKind::UnusedVar), Severity::Error);
}

TEST(Options, GetUnknownThrows) {
    Options opts;
    EXPECT_THROW(opts.get("unknown"), std::invalid_argument);
}

TEST(Options, SetUnknownThrows) {
    Options opts;
    opts.add_option(OptKind::UnusedVar);
    EXPECT_THROW(opts.set("unknown", Severity::Error), std::invalid_argument);
}