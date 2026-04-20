#include "ast_format_parser.hpp"
#include "gencpp/cpp_generator.hpp"
#include "diag/context.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>

// ============================================================================
// CppGenerator Tests — Traditional and Module Output
// ============================================================================

using namespace trust;

// Helper to parse AST from text and build a Program
static std::unique_ptr<Program> parse_program(const std::string& input) {
    Context ctx;
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> roots;
    for (auto& r : *nodes) roots.push_back(r.get());
    return build_ast_from_roots(roots, ctx);
}

static std::unique_ptr<Program> parse_program(const std::string& input, Context& ctx) {
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> roots;
    for (auto& r : *nodes) roots.push_back(r.get());
    return build_ast_from_roots(roots, ctx);
}

// ============================================================================
// Section 1: Traditional C++ Generation
// ============================================================================

TEST(CppGeneratorTest, GenerateVarDecl) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=x type=int\n"
                        "      IntLiteral value=42\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto program = parse_program(input);
    CppGenerator generator;
    std::string code = generator.generate(program.get());
    EXPECT_NE(code.find("int x"), std::string::npos);
    EXPECT_NE(code.find("42"), std::string::npos);
}

TEST(CppGeneratorTest, GenerateBinaryOp) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      BinaryOp op=+ type=int\n"
                        "        IntLiteral value=1\n"
                        "        IntLiteral value=2\n";
    auto program = parse_program(input);
    CppGenerator generator;
    std::string code = generator.generate(program.get());
    EXPECT_NE(code.find("1 + 2"), std::string::npos);
}

TEST(CppGeneratorTest, GenerateIfStmt) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    IfStmt\n"
                        "      VarRef name=x\n"
                        "      ThenBlock\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=1\n"
                        "      ElseBlock\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=0\n";
    auto program = parse_program(input);
    CppGenerator generator;
    std::string code = generator.generate(program.get());
    EXPECT_NE(code.find("if"), std::string::npos);
    EXPECT_NE(code.find("else"), std::string::npos);
}

TEST(CppGeneratorTest, GenerateEmptyProgram) {
    std::vector<std::unique_ptr<Decl>> items;
    Program program(std::move(items));
    CppGenerator generator;
    std::string code = generator.generate(&program);
    EXPECT_FALSE(code.empty());
}

TEST(CppGeneratorTest, GenerateStringLiteral) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ExprStmt\n"
                        "      CallExpr name=print\n"
                        "        StringLiteral value=\"hello\"\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto program = parse_program(input);
    CppGenerator generator;
    std::string code = generator.generate(program.get());
    EXPECT_NE(code.find("hello"), std::string::npos);
}

// ============================================================================
// Section 2: Module Generation (C++20 and C++23)
// ============================================================================

struct ModuleTestParams {
    OutputFormat format;
    const char* module_name;
    const char* expected_module_directive;
    const char* expected_import;
    const char* name_suffix;
};

class CppModuleTest : public testing::TestWithParam<ModuleTestParams> {};

TEST_P(CppModuleTest, ModuleOutput) {
    auto params = GetParam();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto program = parse_program(input);
    GeneratorOptions opts;
    opts.format = params.format;
    opts.module_name = params.module_name;
    CppGenerator generator(opts);
    std::string code = generator.generate(program.get());
    EXPECT_NE(code.find(params.expected_module_directive), std::string::npos);
    if (params.expected_import) {
        EXPECT_NE(code.find(params.expected_import), std::string::npos);
    }
    EXPECT_NE(code.find("export int main()"), std::string::npos);
}

static const ModuleTestParams module_params[] = {
    {OutputFormat::Cpp20Module, "my_module", "export module my_module", "#include", "Simple"},
    {OutputFormat::Cpp20Module, "com.example.myapp", "export module com.example.myapp", "#include", "DottedName"},
    {OutputFormat::Cpp23Module, "my_cpp23_module", "export module my_cpp23_module", "import std;", "Cpp23"},
};

INSTANTIATE_TEST_SUITE_P(ModuleFormats, CppModuleTest,
                         testing::ValuesIn(module_params),
                         [](const testing::TestParamInfo<ModuleTestParams>& info) {
                             return info.param.name_suffix;
                         });

TEST(CppModuleTest, EnumAndStruct) {
    std::string input = "EnumDecl name=Color\n"
                        "  EnumMember name=Red value=1\n"
                        "  EnumMember name=Blue value=2\n"
                        "StructDecl name=Point\n"
                        "  StructField name=x type=int\n"
                        "  StructField name=y type=int\n"
                        "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=c type=Color\n"
                        "      EnumLiteral enum=Color member=Red\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto program = parse_program(input);
    GeneratorOptions opts;
    opts.format = OutputFormat::Cpp20Module;
    opts.module_name = "types_module";
    CppGenerator generator(opts);
    std::string code = generator.generate(program.get());
    EXPECT_NE(code.find("export enum class Color"), std::string::npos);
    EXPECT_NE(code.find("export struct Point"), std::string::npos);
    EXPECT_NE(code.find("export int main()"), std::string::npos);
}

TEST(CppModuleTest, FormatDifferences) {
    std::string input = "FuncDecl name=foo ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=42\n";
    auto program = parse_program(input);
    // Traditional
    CppGenerator trad_gen;
    std::string trad_code = trad_gen.generate(program.get());
    EXPECT_TRUE(trad_code.find("export module") == std::string::npos);
    EXPECT_TRUE(trad_code.find("#include") != std::string::npos);
    // Module
    GeneratorOptions opts;
    opts.format = OutputFormat::Cpp20Module;
    CppGenerator mod_gen(opts);
    std::string mod_code = mod_gen.generate(program.get());
    EXPECT_TRUE(mod_code.find("export module") != std::string::npos);
    EXPECT_TRUE(mod_code.find("export int foo()") != std::string::npos);
    EXPECT_TRUE(trad_code.find("export int foo()") == std::string::npos);
}

TEST(CppModuleTest, Cpp20VsCpp23) {
    std::string input = "FuncDecl name=foo ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=42\n";
    auto program = parse_program(input);
    // C++20
    GeneratorOptions opts20;
    opts20.format = OutputFormat::Cpp20Module;
    opts20.module_name = "test";
    CppGenerator gen20(opts20);
    std::string code20 = gen20.generate(program.get());
    // C++23
    GeneratorOptions opts23;
    opts23.format = OutputFormat::Cpp23Module;
    opts23.module_name = "test";
    CppGenerator gen23(opts23);
    std::string code23 = gen23.generate(program.get());
    EXPECT_NE(code23.find("import std;"), std::string::npos);
    EXPECT_EQ(code20.find("import std;"), std::string::npos);
}