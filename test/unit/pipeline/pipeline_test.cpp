#include "pipeline/pipeline.hpp"
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace trust;

namespace {

// Создаёт временный trust-файл и возвращает путь к нему
std::string create_temp_trust_file() {
    // TEST_DATA_DIR is defined in CMakeLists.txt as "${CMAKE_BINARY_DIR}/test_data"
    std::string base = TEST_DATA_DIR;
    base += "/pipeline_ut_XXXXXX";
    char* tmpl = strdup(base.c_str());
    if (!tmpl)
        return {};
    const char* d = mkdtemp(tmpl);
    std::string dir;
    if (d)
        dir = d;
    free(tmpl);
    if (dir.empty())
        return {};
    return dir + "/input.src";
}

static ParseResult do_parse(std::vector<const char*> args) {
    std::string temp_file = create_temp_trust_file();
    if (temp_file.empty())
        return {};

    // Создаём input.src файл с минимальным содержимым
    {
        std::ofstream ofs(temp_file);
        if (!ofs)
            return {};
        ofs << "x := 42;\n";
    }

    // Заменяем "input.src" на реальный существующий файл
    std::vector<const char*> processed;
    for (auto& a : args) {
        if (a == std::string_view("input.src")) {
            processed.push_back(temp_file.c_str());
        } else {
            processed.push_back(a);
        }
    }

    std::vector<char*> argv;
    argv.reserve(processed.size());
    for (auto& a : processed)
        argv.push_back(const_cast<char*>(a));
    return Pipeline::parseArgs(static_cast<int>(argv.size()), argv.data());
}

} // anonymous namespace

TEST(Parser, HelpShort) {
    auto r = do_parse({"trust", "-h"});
    EXPECT_TRUE(r.opts.help_requested);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(Parser, HelpLong) {
    auto r = do_parse({"trust", "--help"});
    EXPECT_TRUE(r.opts.help_requested);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(Parser, Version) {
    auto r = do_parse({"trust", "--version"});
    EXPECT_TRUE(r.opts.version_requested);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(Parser, VerboseShort) {
    auto r = do_parse({"trust", "-v", "input.src"});
    EXPECT_TRUE(r.opts.verbose);
    EXPECT_FALSE(r.opts.quiet);
}

TEST(Parser, VerboseLong) {
    auto r = do_parse({"trust", "--verbose", "input.src"});
    EXPECT_TRUE(r.opts.verbose);
}

TEST(Parser, QuietShort) {
    auto r = do_parse({"trust", "-q", "input.src"});
    EXPECT_TRUE(r.opts.quiet);
}

TEST(Parser, QuietLong) {
    auto r = do_parse({"trust", "--quiet", "input.src"});
    EXPECT_TRUE(r.opts.quiet);
}

TEST(Parser, OutputShort) {
    auto r = do_parse({"trust", "-o", "output.cpp", "input.src"});
    EXPECT_EQ(r.opts.output_file, "output.cpp");
}

TEST(Parser, OutputLong) {
    auto r = do_parse({"trust", "--output", "output.cpp", "input.src"});
    EXPECT_EQ(r.opts.output_file, "output.cpp");
}

TEST(Parser, OutputEqualsForm) {
    auto r = do_parse({"trust", "--output=output.cpp", "input.src"});
    EXPECT_EQ(r.opts.output_file, "output.cpp");
}

TEST(Parser, EmitTokens) {
    auto r = do_parse({"trust", "--emit-tokens", "input.src"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & EmitFlags::Tokens));
}

TEST(Parser, EmitAST) {
    auto r = do_parse({"trust", "--emit-ast", "input.src"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & EmitFlags::AST));
}

TEST(Parser, EmitCpp) {
    auto r = do_parse({"trust", "--emit-cpp", "input.src"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & EmitFlags::Cpp));
}

TEST(Parser, InputFile) {
    auto r = do_parse({"trust", "input.src"});
    EXPECT_NE(r.opts.input_file, "");
    EXPECT_FALSE(r.opts.input_file.empty());
}

TEST(Parser, NoInputFile) {
    auto r = do_parse({"trust"});
    EXPECT_TRUE(r.opts.input_file.empty());
    EXPECT_EQ(r.exit_code, 1);
}

TEST(Parser, UnknownShort) {
    auto r = do_parse({"trust", "-x", "input.src"});
    bool found = false;
    for (auto& a : r.remaining_args)
        if (a == "-x")
            found = true;
    EXPECT_TRUE(found);
}

TEST(Parser, CombinedFlags) {
    auto r = do_parse({"trust", "-v", "-q", "input.src"});
    EXPECT_TRUE(r.opts.verbose);
    EXPECT_TRUE(r.opts.quiet);
}

TEST(Parser, DiagOptionAsRemaining) {
    auto r = do_parse({"trust", "-Wunused-var", "input.src"});
    bool found = false;
    for (auto& a : r.remaining_args)
        if (a == "-Wunused-var")
            found = true;
    EXPECT_TRUE(found);
}

TEST(Parser, TempDir) {
    auto r = do_parse({"trust", "--temp-dir", "/tmp/trust", "input.src"});
    EXPECT_EQ(r.opts.temp_dir, "/tmp/trust");
}

TEST(Parser, TempDirEqualsForm) {
    auto r = do_parse({"trust", "--temp-dir=/tmp/trust", "input.src"});
    EXPECT_EQ(r.opts.temp_dir, "/tmp/trust");
}

TEST(Parser, Compiler) {
    auto r = do_parse({"trust", "--compiler", "/usr/bin/g++", "input.src"});
    EXPECT_EQ(r.opts.compiler, "/usr/bin/g++");
}

TEST(Parser, CompilerEqualsForm) {
    auto r = do_parse({"trust", "--compiler=/usr/bin/g++", "input.src"});
    EXPECT_EQ(r.opts.compiler, "/usr/bin/g++");
}

TEST(Parser, CompilerDefault) {
    auto r = do_parse({"trust", "input.src"});
    EXPECT_EQ(r.opts.compiler, TRUST_DEFAULT_COMPILER);
}

TEST(Parser, CompileOpts) {
    auto r = do_parse({"trust", "--options", "-Wall -O2", "input.src"});
    EXPECT_EQ(r.opts.compiler_options, "-Wall -O2");
}

TEST(Parser, CompileOptsEqualsForm) {
    auto r = do_parse({"trust", "--options=-Wall -O2", "input.src"});
    EXPECT_EQ(r.opts.compiler_options, "-Wall -O2");
}

TEST(Parser, ObjectFileFlag) {
    auto r = do_parse({"trust", "-c", "input.src"});
    EXPECT_EQ(r.opts.compile_mode, CompileMode::ObjectFile);
}

TEST(Parser, ShouldCompileNoEmit) {
    auto r = do_parse({"trust", "input.src"});
    EXPECT_TRUE(r.opts.should_compile());
}

TEST(Parser, ShouldCompileWithEmitCpp) {
    auto r = do_parse({"trust", "--emit-cpp", "input.src"});
    EXPECT_FALSE(r.opts.should_compile());
}

TEST(Parser, ShouldCompileWithEmitFlags) {
    auto r = do_parse({"trust", "--emit-ast", "--emit-cpp", "input.src"});
    EXPECT_FALSE(r.opts.should_compile());
}

TEST(Parser, CombinedCompileOptions) {
    auto r = do_parse({"trust", "-c", "--compiler=/usr/bin/g++", "--temp-dir=/tmp", "--options=-O2", "input.src"});
    EXPECT_EQ(r.opts.compile_mode, CompileMode::ObjectFile);
    EXPECT_EQ(r.opts.compiler, "/usr/bin/g++");
    EXPECT_EQ(r.opts.temp_dir, "/tmp");
    EXPECT_EQ(r.opts.compiler_options, "-O2");
    EXPECT_TRUE(r.opts.should_compile());
}

TEST(Parser, StaticLibShort) {
    auto r = do_parse({"trust", "-a", "input.src"});
    EXPECT_EQ(r.opts.compile_mode, CompileMode::StaticLib);
    EXPECT_NE(r.opts.compile_mode, CompileMode::SharedLib);
}

TEST(Parser, StaticLibLong) {
    auto r = do_parse({"trust", "--static-lib", "input.src"});
    EXPECT_EQ(r.opts.compile_mode, CompileMode::StaticLib);
}

TEST(Parser, SharedLibShort) {
    auto r = do_parse({"trust", "-l", "input.src"});
    EXPECT_EQ(r.opts.compile_mode, CompileMode::SharedLib);
    EXPECT_NE(r.opts.compile_mode, CompileMode::StaticLib);
}

TEST(Parser, SharedLibLong) {
    auto r = do_parse({"trust", "--shared-lib", "input.src"});
    EXPECT_EQ(r.opts.compile_mode, CompileMode::SharedLib);
}

TEST(Parser, DslDefault) {
    auto r = do_parse({"trust", "input.src"});
    EXPECT_FALSE(r.opts.no_dsl);
    EXPECT_TRUE(r.opts.dsl_file.empty());
}

TEST(Parser, DslWithValue) {
    auto r = do_parse({"trust", "--dsl", "custom.src", "input.src"});
    EXPECT_EQ(r.opts.dsl_file, "custom.src");
    EXPECT_FALSE(r.opts.no_dsl);
}

TEST(Parser, DslEqualsForm) {
    auto r = do_parse({"trust", "--dsl=custom.src", "input.src"});
    EXPECT_EQ(r.opts.dsl_file, "custom.src");
    EXPECT_FALSE(r.opts.no_dsl);
}

TEST(Parser, NoDsl) {
    auto r = do_parse({"trust", "--no-dsl", "input.src"});
    EXPECT_TRUE(r.opts.no_dsl);
    EXPECT_TRUE(r.opts.dsl_file.empty());
}

TEST(Parser, DslAndNoDslConflict) {
    auto r = do_parse({"trust", "--dsl", "custom.src", "--no-dsl", "input.src"});
    EXPECT_EQ(r.exit_code, 1);
}
