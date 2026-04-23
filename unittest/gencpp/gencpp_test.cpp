import trust;

#include "ast_format_parser.hpp"
#include "types/type_info.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace trust;

static std::unique_ptr<Context> make_context() {
    auto ctx = std::make_unique<Context>();
    ctx->diag().setOutput(&std::cerr);
    return ctx;
}

static std::unique_ptr<Program> parse_program(const std::string &input, Context &ctx) {
    auto nodes = parse_ast_format(input, ctx);
    if (!nodes.has_value())
        return nullptr;
    std::vector<ParsedNode *> roots;
    for (auto &r : *nodes)
        roots.push_back(r.get());
    return build_ast_from_roots(roots, ctx);
}

// ============================================================================
// GenCpp Tests (using CppGenerator and print_ast directly)
// ============================================================================

TEST(GencppTest, DefaultConstruction) {
    CppGenerator gen;
    EXPECT_TRUE(true);
}

TEST(GencppTest, ParseAndBuild) {
    auto ctx = make_context();
    std::string input = "VarDecl name=x type=int\n"
                        "  IntLiteral value=42\n";
    auto program = parse_program(input, *ctx);
    EXPECT_NE(program, nullptr);
}

TEST(GencppTest, GetAstText) {
    auto ctx = make_context();
    std::string input = "VarDecl name=x type=int\n"
                        "  IntLiteral value=42\n";
    auto program = parse_program(input, *ctx);
    ASSERT_NE(program, nullptr);

    std::string text = print_ast(program.get());
    EXPECT_NE(text.find("VarDecl"), std::string::npos);
    EXPECT_NE(text.find("name=x"), std::string::npos);
}

TEST(GencppTest, GenerateCppCode) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto program = parse_program(input, *ctx);
    ASSERT_NE(program, nullptr);

    CppGenerator gen;
    std::string code = gen.generate(program.get());
    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("main"), std::string::npos);
}

TEST(GencppTest, EmptyProgram) {
    CppGenerator gen;
    std::vector<std::unique_ptr<Decl>> items;
    Program program(std::move(items));
    std::string text = print_ast(&program);
    EXPECT_FALSE(text.empty());
    std::string code = gen.generate(&program);
    EXPECT_FALSE(code.empty());
}

TEST(GencppTest, ComplexFunction) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=calculate ret=int\n"
                        "  ParamDecl name=a type=int\n"
                        "  ParamDecl name=b type=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=result type=int\n"
                        "      BinaryOp op=* type=int\n"
                        "        VarRef name=a\n"
                        "        VarRef name=b\n"
                        "    ReturnStmt\n"
                        "      VarRef name=result\n";
    auto program = parse_program(input, *ctx);
    ASSERT_NE(program, nullptr);

    CppGenerator gen;
    std::string code = gen.generate(program.get());
    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("calculate"), std::string::npos);
}

TEST(GencppTest, FuncWithParams) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=add ret=int\n"
                        "  ParamDecl name=a type=int\n"
                        "  ParamDecl name=b type=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      BinaryOp op=+ type=int\n"
                        "        VarRef name=a\n"
                        "        VarRef name=b\n";
    auto program = parse_program(input, *ctx);
    ASSERT_NE(program, nullptr);

    ASSERT_TRUE(program->items[0]->is<FuncDecl>());
    auto *func = program->items[0]->as<FuncDecl>();
    EXPECT_EQ(func->params.size(), 2);
    EXPECT_EQ(func->params[0]->name, "a");
    EXPECT_EQ(func->params[1]->name, "b");
}

TEST(GencppTest, IfStmt) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=x type=int\n"
                        "      IntLiteral value=10\n"
                        "    IfStmt\n"
                        "      BinaryOp op=> type=bool\n"
                        "        VarRef name=x\n"
                        "        IntLiteral value=5\n"
                        "      ThenBlock\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=1\n"
                        "      ElseBlock\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=0\n";
    auto program = parse_program(input, *ctx);
    ASSERT_NE(program, nullptr);

    CppGenerator gen;
    std::string code = gen.generate(program.get());
    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("if"), std::string::npos);
    EXPECT_NE(code.find("else"), std::string::npos);
}

TEST(GencppTest, ElseIfChain) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=x type=int\n"
                        "      IntLiteral value=10\n"
                        "    IfStmt\n"
                        "      BinaryOp op=>= type=bool\n"
                        "        VarRef name=x\n"
                        "        IntLiteral value=10\n"
                        "      ThenBlock\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=1\n"
                        "      ElseBlock\n"
                        "        IfStmt\n"
                        "          BinaryOp op=> type=bool\n"
                        "            VarRef name=x\n"
                        "            IntLiteral value=20\n"
                        "          ThenBlock\n"
                        "            ReturnStmt\n"
                        "              IntLiteral value=2\n"
                        "          ElseBlock\n"
                        "            ReturnStmt\n"
                        "              IntLiteral value=0\n";
    auto program = parse_program(input, *ctx);
    ASSERT_NE(program, nullptr);

    CppGenerator gen;
    std::string code = gen.generate(program.get());
    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("else if"), std::string::npos);
}

// ============================================================================
// TypeRequirements Tests
// ============================================================================

TEST(TypeRequirementsTest, BuiltinIntHasNoRequirements) {
    auto &reg = TypeRequirementsRegistry::instance();
    const auto &req = reg.get(TypeKind::Int);
    EXPECT_TRUE(req.headers.empty());
    EXPECT_TRUE(req.link_libs.empty());
    EXPECT_EQ(req.min_format, OutputFormat::Traditional);
}

TEST(TypeRequirementsTest, StringHasNoAutoHeaders) {
    // Headers are not in X-macro; they must be registered explicitly
    auto &reg = TypeRequirementsRegistry::instance();
    const auto &req = reg.get(TypeKind::String);
    EXPECT_TRUE(req.headers.empty());
    EXPECT_EQ(req.min_format, OutputFormat::Traditional);
}

TEST(TypeRequirementsTest, ExpectedRequiresCpp23Module) {
    auto &reg = TypeRequirementsRegistry::instance();
    const auto &req = reg.get(TypeKind::Expected);
    EXPECT_TRUE(req.headers.empty()); // headers not in X-macro
    EXPECT_EQ(req.min_format, OutputFormat::Cpp23Module);
}

TEST(TypeRequirementsTest, CustomRegistration) {
    auto &reg = TypeRequirementsRegistry::instance();
    TypeRequirements custom_req;
    custom_req.headers = {"<custom.h>", "<another.h>"};
    custom_req.link_libs = {"mylib"};
    custom_req.min_format = OutputFormat::Cpp20Module;
    reg.register_type(TypeKind::UserType, custom_req);

    const auto &retrieved = reg.get(TypeKind::UserType);
    ASSERT_EQ(retrieved.headers.size(), 2);
    EXPECT_EQ(retrieved.headers[0], "<custom.h>");
    EXPECT_EQ(retrieved.headers[1], "<another.h>");
    EXPECT_EQ(retrieved.link_libs[0], "mylib");
    EXPECT_EQ(retrieved.min_format, OutputFormat::Cpp20Module);
}

TEST(TypeRequirementsTest, CollectHeadersFromUsedTypes) {
    // Register a type with headers
    TypeRequirements str_req(TypeKind::UserType, OutputFormat::Traditional, {"<string>"});
    auto &reg = TypeRequirementsRegistry::instance();
    reg.register_type(TypeKind::UserType, str_req);

    std::vector<TypeKind> used = {TypeKind::Int, TypeKind::UserType, TypeKind::Bool};
    auto headers = reg.collect_headers(used);
    ASSERT_EQ(headers.size(), 1);
    EXPECT_EQ(headers[0], "<string>");
}

TEST(TypeRequirementsTest, CollectLinkLibs) {
    TypeRequirements req(TypeKind::UserType, OutputFormat::Traditional, {}, {"pthread", "dl"});
    auto &reg = TypeRequirementsRegistry::instance();
    reg.register_type(TypeKind::UserType, req);

    std::vector<TypeKind> used = {TypeKind::Int, TypeKind::UserType};
    auto libs = reg.collect_link_libs(used);
    ASSERT_EQ(libs.size(), 2);
    EXPECT_EQ(libs[0], "pthread");
    EXPECT_EQ(libs[1], "dl");
}

TEST(TypeRequirementsTest, FormatCompatibility) {
    auto &reg = TypeRequirementsRegistry::instance();
    EXPECT_TRUE(reg.is_format_compatible(TypeKind::Int, OutputFormat::Traditional));
    EXPECT_TRUE(reg.is_format_compatible(TypeKind::Expected, OutputFormat::Cpp23Module));
    // Expected requires Cpp23Module, so Traditional is not compatible
    EXPECT_FALSE(reg.is_format_compatible(TypeKind::Expected, OutputFormat::Traditional));
}

TEST(TypeRequirementsTest, TypeInfoRequirements) {
    TypeInfo int_ti = TypeInfo::builtin(TypeKind::Int);
    TypeInfo str_ti = TypeInfo::builtin(TypeKind::String);

    EXPECT_TRUE(int_ti.requirements().headers.empty());
    EXPECT_TRUE(str_ti.requirements().headers.empty()); // headers must be registered explicitly
}
