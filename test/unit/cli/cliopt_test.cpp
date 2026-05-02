#include "cli/option_cli_def.hpp"
#include "diag/options.hpp"
#include <gtest/gtest.h>

using namespace trust;

// Проверка: ни одно длинное имя CLI не совпадает с именем diag-опции
TEST(CliOptOverlap, NoLongNameMatchesDiag) {
    Options opts; // Создаёт все diag-опции из OPTIONS_LIST
    for (int i = 0; i < NumCliOptions; ++i) {
        auto &meta = all_cli_opts[i];
        if (!meta.long_name.empty()) {
            EXPECT_FALSE(opts.is_registered(meta.long_name)) << "CLI --" << meta.long_name << " overlaps with diag option";
        }
    }
}

// Проверка: ни одно короткое имя CLI не совпадает с именем diag-опции
TEST(CliOptOverlap, NoShortNameMatchesDiag) {
    Options opts;
    for (int i = 0; i < NumCliOptions; ++i) {
        auto &meta = all_cli_opts[i];
        if (!meta.short_name.empty()) {
            EXPECT_FALSE(opts.is_registered(meta.short_name)) << "CLI -" << meta.short_name << " overlaps with diag option";
        }
    }
}

// Проверка: ни одна CLI опция не начинается с "W" (префикс diag)
TEST(CliOptOverlap, NoCliStartsWithW) {
    for (int i = 0; i < NumCliOptions; ++i) {
        auto &meta = all_cli_opts[i];
        if (!meta.long_name.empty()) {
            EXPECT_FALSE(meta.long_name.starts_with("W") || meta.long_name.starts_with("w")) << "CLI --" << meta.long_name << " starts with 'W'";
        }
        if (!meta.short_name.empty()) {
            EXPECT_FALSE(meta.short_name[0] == 'W' || meta.short_name[0] == 'w') << "CLI -" << meta.short_name << " starts with 'W'";
        }
    }
}

// Проверка: все CLI опции имеют ненулевые метаданные
TEST(CliOptOverlap, AllOptsHaveMeta) {
    for (int i = 0; i < NumCliOptions; ++i) {
        auto &meta = all_cli_opts[i];
        EXPECT_FALSE(meta.name.empty()) << "Opt " << i << " has empty name";
        EXPECT_FALSE(meta.description.empty()) << "Opt " << i << " has empty description";
    }
}

// Проверка: enum значения соответствует индексам в массиве
TEST(CliOptOverlap, EnumMatchesArrayIndex) {
    EXPECT_EQ(static_cast<int>(CliOpt::Input), 0);
    EXPECT_EQ(static_cast<int>(CliOpt::Output), 1);
    EXPECT_EQ(static_cast<int>(CliOpt::Verbose), 2);
    EXPECT_EQ(static_cast<int>(CliOpt::Quiet), 3);
    EXPECT_EQ(static_cast<int>(CliOpt::EmitTokens), 4);
    EXPECT_EQ(static_cast<int>(CliOpt::EmitAST), 5);
    EXPECT_EQ(static_cast<int>(CliOpt::EmitCpp), 6);
    EXPECT_EQ(static_cast<int>(CliOpt::EmitModule), 7);
    EXPECT_EQ(static_cast<int>(CliOpt::Help), 8);
    EXPECT_EQ(static_cast<int>(CliOpt::Version), 9);
    EXPECT_EQ(static_cast<int>(CliOpt::TempDir), 10);
    EXPECT_EQ(static_cast<int>(CliOpt::Compiler), 11);
    EXPECT_EQ(static_cast<int>(CliOpt::CompileOpts), 12);
    EXPECT_EQ(static_cast<int>(CliOpt::ObjectFile), 13);
    EXPECT_EQ(static_cast<int>(CliOpt::StaticLib), 14);
    EXPECT_EQ(static_cast<int>(CliOpt::SharedLib), 15);
    EXPECT_EQ(static_cast<int>(CliOpt::BindingHeader), 16);
    EXPECT_EQ(static_cast<int>(CliOpt::NoBindingHeader), 17);
    EXPECT_EQ(NumCliOptions, 18);
}

// Проверка: lookup по short/long name работает корректно
TEST(CliOptOverlap, LookupByName) {
    EXPECT_EQ(cli_opt_from_short("v"), CliOpt::Verbose);
    EXPECT_EQ(cli_opt_from_short("o"), CliOpt::Output);
    EXPECT_EQ(cli_opt_from_short("h"), CliOpt::Help);

    EXPECT_EQ(cli_opt_from_long("verbose"), CliOpt::Verbose);
    EXPECT_EQ(cli_opt_from_long("output"), CliOpt::Output);
    EXPECT_EQ(cli_opt_from_long("help"), CliOpt::Help);
    EXPECT_EQ(cli_opt_from_long("emit-tokens"), CliOpt::EmitTokens);
    EXPECT_EQ(cli_opt_from_long("emit-ast"), CliOpt::EmitAST);
    EXPECT_EQ(cli_opt_from_long("emit-cpp"), CliOpt::EmitCpp);
    EXPECT_EQ(cli_opt_from_long("emit-module"), CliOpt::EmitModule);
    EXPECT_EQ(cli_opt_from_long("version"), CliOpt::Version);

    // Несуществующие — возвращают Input (sentinel)
    EXPECT_EQ(cli_opt_from_short("z"), CliOpt::Input);
    EXPECT_EQ(cli_opt_from_long("nonexistent"), CliOpt::Input);
}