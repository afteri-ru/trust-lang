#include "pipeline/pipeline_parser.hpp"
#include "pipeline/options.h"
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace trust;

namespace {

// Создаёт временный trust-файл и возвращает путь к нему
std::string create_temp_trust_file() {
    auto tmp = std::filesystem::temp_directory_path() / "test_input_XXXXXX.trust";
    // mkstemp-style
    char tmpl[256];
    std::snprintf(tmpl, sizeof(tmpl), "%s/test_input_XXXXXX.trust", std::filesystem::temp_directory_path().c_str());
    int fd = mkstemps(tmpl, 6); // 6 = длина ".trust"
    if (fd == -1) {
        perror("mkstemps");
        return {};
    }
    close(fd);
    return tmpl;
}

static ParseResult do_parse(std::vector<const char*> args) {
    std::string temp_file;

    // Заменяем "input.trust" на реальный существующий файл
    std::vector<const char*> processed;
    for (auto& a : args) {
        if (a == std::string_view("input.trust")) {
            if (temp_file.empty())
                temp_file = create_temp_trust_file();
            processed.push_back(temp_file.c_str());
        } else {
            processed.push_back(a);
        }
    }

    std::vector<char*> argv;
    argv.reserve(processed.size());
    for (auto& a : processed)
        argv.push_back(const_cast<char*>(a));
    return parse_args(static_cast<int>(argv.size()), argv.data());
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
    auto r = do_parse({"trust", "-v", "input.trust"});
    EXPECT_TRUE(r.opts.verbose);
    EXPECT_FALSE(r.opts.quiet);
}

TEST(Parser, VerboseLong) {
    auto r = do_parse({"trust", "--verbose", "input.trust"});
    EXPECT_TRUE(r.opts.verbose);
}

TEST(Parser, QuietShort) {
    auto r = do_parse({"trust", "-q", "input.trust"});
    EXPECT_TRUE(r.opts.quiet);
}

TEST(Parser, QuietLong) {
    auto r = do_parse({"trust", "--quiet", "input.trust"});
    EXPECT_TRUE(r.opts.quiet);
}

TEST(Parser, OutputShort) {
    auto r = do_parse({"trust", "-o", "output.cpp", "input.trust"});
    EXPECT_EQ(r.opts.output_file, "output.cpp");
}

TEST(Parser, OutputLong) {
    auto r = do_parse({"trust", "--output", "output.cpp", "input.trust"});
    EXPECT_EQ(r.opts.output_file, "output.cpp");
}

TEST(Parser, OutputEqualsForm) {
    auto r = do_parse({"trust", "--output=output.cpp", "input.trust"});
    EXPECT_EQ(r.opts.output_file, "output.cpp");
}

TEST(Parser, EmitTokens) {
    auto r = do_parse({"trust", "--emit-tokens", "input.trust"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & EmitFlags::Tokens));
}

TEST(Parser, EmitAST) {
    auto r = do_parse({"trust", "--emit-ast", "input.trust"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & EmitFlags::AST));
}

TEST(Parser, EmitCpp) {
    auto r = do_parse({"trust", "--emit-cpp", "input.trust"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & EmitFlags::Cpp));
}

TEST(Parser, EmitModule) {
    auto r = do_parse({"trust", "--emit-module", "input.trust"});
    EXPECT_TRUE(static_cast<int>(r.opts.emit_flags & EmitFlags::Module));
}

TEST(Parser, InputFile) {
    auto r = do_parse({"trust", "input.trust"});
    EXPECT_NE(r.opts.input_file, "");
    EXPECT_FALSE(r.opts.input_file.empty());
}

TEST(Parser, NoInputFile) {
    auto r = do_parse({"trust"});
    EXPECT_TRUE(r.opts.input_file.empty());
    EXPECT_EQ(r.exit_code, 1);
}

TEST(Parser, UnknownShort) {
    auto r = do_parse({"trust", "-x", "input.trust"});
    bool found = false;
    for (auto& a : r.remaining_args)
        if (a == "-x")
            found = true;
    EXPECT_TRUE(found);
}

TEST(Parser, CombinedFlags) {
    auto r = do_parse({"trust", "-v", "-q", "input.trust"});
    EXPECT_TRUE(r.opts.verbose);
    EXPECT_TRUE(r.opts.quiet);
}

TEST(Parser, DiagOptionAsRemaining) {
    auto r = do_parse({"trust", "-Wunused-var", "input.trust"});
    bool found = false;
    for (auto& a : r.remaining_args)
        if (a == "-Wunused-var")
            found = true;
    EXPECT_TRUE(found);
}

TEST(Parser, TempDir) {
    auto r = do_parse({"trust", "--temp-dir", "/tmp/trust", "input.trust"});
    EXPECT_EQ(r.opts.temp_dir, "/tmp/trust");
}

TEST(Parser, TempDirEqualsForm) {
    auto r = do_parse({"trust", "--temp-dir=/tmp/trust", "input.trust"});
    EXPECT_EQ(r.opts.temp_dir, "/tmp/trust");
}

TEST(Parser, Compiler) {
    auto r = do_parse({"trust", "--compiler", "/usr/bin/g++", "input.trust"});
    EXPECT_EQ(r.opts.compiler, "/usr/bin/g++");
}

TEST(Parser, CompilerEqualsForm) {
    auto r = do_parse({"trust", "--compiler=/usr/bin/g++", "input.trust"});
    EXPECT_EQ(r.opts.compiler, "/usr/bin/g++");
}

TEST(Parser, CompilerDefault) {
    auto r = do_parse({"trust", "input.trust"});
    EXPECT_EQ(r.opts.compiler, TRUST_DEFAULT_COMPILER);
}

TEST(Parser, CompileOpts) {
    auto r = do_parse({"trust", "--options", "-Wall -O2", "input.trust"});
    EXPECT_EQ(r.opts.compiler_options, "-Wall -O2");
}

TEST(Parser, CompileOptsEqualsForm) {
    auto r = do_parse({"trust", "--options=-Wall -O2", "input.trust"});
    EXPECT_EQ(r.opts.compiler_options, "-Wall -O2");
}

TEST(Parser, ObjectFileFlag) {
    auto r = do_parse({"trust", "-c", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_object);
}

TEST(Parser, ShouldCompileNoEmit) {
    auto r = do_parse({"trust", "input.trust"});
    EXPECT_TRUE(r.opts.should_compile());
}

TEST(Parser, ShouldCompileWithEmitCpp) {
    auto r = do_parse({"trust", "--emit-cpp", "input.trust"});
    EXPECT_FALSE(r.opts.should_compile());
}

TEST(Parser, ShouldCompileWithEmitFlags) {
    auto r = do_parse({"trust", "--emit-ast", "--emit-cpp", "input.trust"});
    EXPECT_FALSE(r.opts.should_compile());
}

TEST(Parser, CombinedCompileOptions) {
    auto r = do_parse({"trust", "-c", "--compiler=/usr/bin/g++", "--temp-dir=/tmp", "--options=-O2", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_object);
    EXPECT_EQ(r.opts.compiler, "/usr/bin/g++");
    EXPECT_EQ(r.opts.temp_dir, "/tmp");
    EXPECT_EQ(r.opts.compiler_options, "-O2");
    EXPECT_TRUE(r.opts.should_compile());
}

TEST(Parser, StaticLibShort) {
    auto r = do_parse({"trust", "-a", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_static_lib);
    EXPECT_FALSE(r.opts.compile_to_shared_lib);
    EXPECT_TRUE(r.opts.gen_binding_header);
}

TEST(Parser, StaticLibLong) {
    auto r = do_parse({"trust", "--static-lib", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_static_lib);
    EXPECT_TRUE(r.opts.gen_binding_header);
}

TEST(Parser, SharedLibShort) {
    auto r = do_parse({"trust", "-l", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_shared_lib);
    EXPECT_FALSE(r.opts.compile_to_static_lib);
    EXPECT_TRUE(r.opts.gen_binding_header);
}

TEST(Parser, SharedLibLong) {
    auto r = do_parse({"trust", "--shared-lib", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_shared_lib);
    EXPECT_TRUE(r.opts.gen_binding_header);
}

TEST(Parser, BindingHeaderShort) {
    auto r = do_parse({"trust", "-b", "input.trust"});
    EXPECT_TRUE(r.opts.gen_binding_header);
    EXPECT_TRUE(r.opts.binding_header_file.empty());
}

TEST(Parser, BindingHeaderWithFile) {
    auto r = do_parse({"trust", "--binding-header=custom.h", "input.trust"});
    EXPECT_TRUE(r.opts.gen_binding_header);
    EXPECT_EQ(r.opts.binding_header_file, "custom.h");
}

TEST(Parser, BindingHeaderNoFileLong) {
    // --binding-header без = должен работать как флаг
    auto r = do_parse({"trust", "input.trust", "--binding-header"});
    EXPECT_TRUE(r.opts.gen_binding_header);
    EXPECT_TRUE(r.opts.binding_header_file.empty());
}

TEST(Parser, NoBindingHeader) {
    auto r = do_parse({"trust", "-a", "--no-binding-header", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_static_lib);
    EXPECT_FALSE(r.opts.gen_binding_header);
}

TEST(Parser, SharedLibWithBindingHeaderFile) {
    auto r = do_parse({"trust", "-l", "--binding-header=my_api.h", "input.trust"});
    EXPECT_TRUE(r.opts.compile_to_shared_lib);
    EXPECT_TRUE(r.opts.gen_binding_header);
    EXPECT_EQ(r.opts.binding_header_file, "my_api.h");
}