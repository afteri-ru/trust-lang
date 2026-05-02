#include "cli/cli_parser.hpp"
#include "cli/options.h"
#include <gtest/gtest.h>

using namespace trust;

// Helper для conversion argv
static CliParseResult parse_args(std::vector<const char *> args) {
    std::vector<char *> argv;
    argv.reserve(args.size());
    for (auto &a : args)
        argv.push_back(const_cast<char *>(a));
    return parse_cli_args(static_cast<int>(argv.size()), argv.data());
}

TEST(CliParser, HelpShort) {
    auto r = parse_args({"trust", "-h"});
    EXPECT_TRUE(r.opts.help_requested);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(CliParser, HelpLong) {
    auto r = parse_args({"trust", "--help"});
    EXPECT_TRUE(r.opts.help_requested);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(CliParser, Version) {
    auto r = parse_args({"trust", "--version"});
    EXPECT_TRUE(r.opts.version_requested);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(CliParser, VerboseShort) {
    auto r = parse_args({"trust", "-v", "input.trust"});
    EXPECT_TRUE(r.opts.verbose);
    EXPECT_FALSE(r.opts.quiet);
}

TEST(CliParser, VerboseLong) {
    auto r = parse_args({"trust", "--verbose", "input.trust"});
    EXPECT_TRUE(r.opts.verbose);
}

TEST(CliParser, QuietShort) {
    auto r = parse_args({"trust", "-q", "input.trust"});
    EXPECT_TRUE(r.opts.quiet);
}

TEST(CliParser, QuietLong) {
    auto r = parse_args({"trust", "--quiet", "input.trust"});
    EXPECT_TRUE(r.opts.quiet);
}

TEST(CliParser, OutputShort) {
    auto r = parse_args({"trust", "-o", "output.cpp", "input.trust"});
    EXPECT_EQ(r.opts.output_file, "output.cpp");
}

TEST(CliParser, OutputLong) {
    auto r = parse_args({"trust", "--output", "output.cpp", "input.trust"});
    EXPECT_EQ(r.opts.output_file, "output.cpp");
}

TEST(CliParser, OutputEqualsForm) {
    auto r = parse_args({"trust", "--output=output.cpp", "input.trust"});
    EXPECT_EQ(r.opts.output_file, "output.cpp");
}

TEST(CliParser, EmitTokens) {
    auto r = parse_args({"trust", "--emit-tokens", "input.trust"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & CliEmitFlags::Tokens));
}

TEST(CliParser, EmitAST) {
    auto r = parse_args({"trust", "--emit-ast", "input.trust"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & CliEmitFlags::AST));
}

TEST(CliParser, EmitCpp) {
    auto r = parse_args({"trust", "--emit-cpp", "input.trust"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & CliEmitFlags::Cpp));
}

TEST(CliParser, EmitModule) {
    auto r = parse_args({"trust", "--emit-module", "input.trust"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & CliEmitFlags::Module));
}

TEST(CliParser, InputFile) {
    auto r = parse_args({"trust", "input.trust"});
    EXPECT_EQ(r.opts.input_file, "input.trust");
}

TEST(CliParser, NoInputFile) {
    auto r = parse_args({"trust"});
    EXPECT_TRUE(r.opts.input_file.empty());
    EXPECT_EQ(r.exit_code, 1);
}

TEST(CliParser, UnknownShort) {
    auto r = parse_args({"trust", "-x", "input.trust"});
    // -x не распознана — должна попасть в remaining_args
    bool found = false;
    for (auto &a : r.remaining_args)
        if (a == "-x")
            found = true;
    EXPECT_TRUE(found);
}

TEST(CliParser, CombinedFlags) {
    auto r = parse_args({"trust", "-v", "-q", "input.trust"});
    EXPECT_TRUE(r.opts.verbose);
    EXPECT_TRUE(r.opts.quiet);
}

TEST(CliParser, DiagOptionAsRemaining) {
    auto r = parse_args({"trust", "-Wunused-var", "input.trust"});
    bool found = false;
    for (auto &a : r.remaining_args)
        if (a == "-Wunused-var")
            found = true;
    EXPECT_TRUE(found);
}

TEST(CliParser, TempDir) {
    auto r = parse_args({"trust", "--temp-dir", "/tmp/trust", "input.trust"});
    EXPECT_EQ(r.opts.temp_dir, "/tmp/trust");
}

TEST(CliParser, TempDirEqualsForm) {
    auto r = parse_args({"trust", "--temp-dir=/tmp/trust", "input.trust"});
    EXPECT_EQ(r.opts.temp_dir, "/tmp/trust");
}

TEST(CliParser, Compiler) {
    auto r = parse_args({"trust", "--compiler", "/usr/bin/g++", "input.trust"});
    EXPECT_EQ(r.opts.compiler, "/usr/bin/g++");
}

TEST(CliParser, CompilerEqualsForm) {
    auto r = parse_args({"trust", "--compiler=/usr/bin/g++", "input.trust"});
    EXPECT_EQ(r.opts.compiler, "/usr/bin/g++");
}

TEST(CliParser, CompilerDefault) {
    auto r = parse_args({"trust", "input.trust"});
    EXPECT_EQ(r.opts.compiler, TRUST_DEFAULT_COMPILER);
}

TEST(CliParser, CompileOpts) {
    auto r = parse_args({"trust", "--options", "-Wall -O2", "input.trust"});
    EXPECT_EQ(r.opts.compiler_options, "-Wall -O2");
}

TEST(CliParser, CompileOptsEqualsForm) {
    auto r = parse_args({"trust", "--options=-Wall -O2", "input.trust"});
    EXPECT_EQ(r.opts.compiler_options, "-Wall -O2");
}

TEST(CliParser, ObjectFileFlag) {
    auto r = parse_args({"trust", "-c", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_object);
}

TEST(CliParser, ShouldCompileNoEmit) {
    auto r = parse_args({"trust", "input.trust"});
    EXPECT_TRUE(r.opts.should_compile());
}

TEST(CliParser, ShouldCompileWithEmitCpp) {
    auto r = parse_args({"trust", "--emit-cpp", "input.trust"});
    EXPECT_FALSE(r.opts.should_compile());
}

TEST(CliParser, ShouldCompileWithEmitFlags) {
    auto r = parse_args({"trust", "--emit-ast", "--emit-cpp", "input.trust"});
    EXPECT_FALSE(r.opts.should_compile());
}

TEST(CliParser, CombinedCompileOptions) {
    auto r = parse_args({"trust", "-c", "--compiler=/usr/bin/g++", "--temp-dir=/tmp", "--options=-O2", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_object);
    EXPECT_EQ(r.opts.compiler, "/usr/bin/g++");
    EXPECT_EQ(r.opts.temp_dir, "/tmp");
    EXPECT_EQ(r.opts.compiler_options, "-O2");
    EXPECT_TRUE(r.opts.should_compile());
}

// StaticLib tests
TEST(CliParser, StaticLibShort) {
    auto r = parse_args({"trust", "-a", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_static_lib);
    EXPECT_FALSE(r.opts.compile_to_shared_lib);
    EXPECT_TRUE(r.opts.gen_binding_header); // auto-enabled for libraries
}

TEST(CliParser, StaticLibLong) {
    auto r = parse_args({"trust", "--static-lib", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_static_lib);
    EXPECT_TRUE(r.opts.gen_binding_header);
}

// SharedLib tests
TEST(CliParser, SharedLibShort) {
    auto r = parse_args({"trust", "-l", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_shared_lib);
    EXPECT_FALSE(r.opts.compile_to_static_lib);
    EXPECT_TRUE(r.opts.gen_binding_header); // auto-enabled for libraries
}

TEST(CliParser, SharedLibLong) {
    auto r = parse_args({"trust", "--shared-lib", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_shared_lib);
    EXPECT_TRUE(r.opts.gen_binding_header);
}

// BindingHeader tests
TEST(CliParser, BindingHeaderShort) {
    auto r = parse_args({"trust", "-b", "input.trust"});
    EXPECT_TRUE(r.opts.gen_binding_header);
    EXPECT_TRUE(r.opts.binding_header_file.empty()); // default to input file name
}

TEST(CliParser, BindingHeaderWithFile) {
    auto r = parse_args({"trust", "--binding-header=custom.h", "input.trust"});
    EXPECT_TRUE(r.opts.gen_binding_header);
    EXPECT_EQ(r.opts.binding_header_file, "custom.h");
}

TEST(CliParser, BindingHeaderNoFileLong) {
    auto r = parse_args({"trust", "--binding-header", "input.trust"});
    EXPECT_TRUE(r.opts.gen_binding_header);
    EXPECT_TRUE(r.opts.binding_header_file.empty());
}

TEST(CliParser, NoBindingHeader) {
    auto r = parse_args({"trust", "-a", "--no-binding-header", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_static_lib);
    EXPECT_FALSE(r.opts.gen_binding_header); // explicitly disabled
}

TEST(CliParser, SharedLibWithBindingHeaderFile) {
    auto r = parse_args({"trust", "-l", "--binding-header=my_api.h", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_shared_lib);
    EXPECT_TRUE(r.opts.gen_binding_header);
    EXPECT_EQ(r.opts.binding_header_file, "my_api.h");
}
