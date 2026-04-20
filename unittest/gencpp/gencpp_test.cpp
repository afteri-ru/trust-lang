import trust;

#include "ast_format_parser.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace trust;

static std::unique_ptr<Context> make_context() {
    auto ctx = std::make_unique<Context>();
    ctx->diag().setOutput(&std::cerr);
    return ctx;
}

static std::unique_ptr<Program> parse_program(const std::string& input, Context& ctx) {
    auto nodes = parse_ast_format(input, ctx);
    if (!nodes.has_value()) return nullptr;
    std::vector<ParsedNode*> roots;
    for (auto& r : *nodes) roots.push_back(r.get());
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

    auto *func = dynamic_cast<const FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
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