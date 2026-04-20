#include "ast_format_parser.hpp"
#include "gencpp/ast_printer.hpp"
#include "gencpp/cpp_generator.hpp"
#include "diag/context.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace trust;

// ============================================================================
// AST Printing Tests
// ============================================================================

class AstPrintingTest : public ::testing::Test {
protected:
    Context ctx;
};

// Test: Print AstPrintingTest::PrintVarDecl round-trip
TEST_F(AstPrintingTest, PrintVarDecl) {
    std::string input = "VarDecl name=x type=int\n"
                        "  IntLiteral value=42\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    std::string output = print_ast(program.get());
    EXPECT_NE(output.find("VarDecl"), std::string::npos);
    EXPECT_NE(output.find("name=x"), std::string::npos);
    EXPECT_NE(output.find("type=int"), std::string::npos);
}

// Test: Print AstPrintingTest::FuncDecl round-trip
TEST_F(AstPrintingTest, PrintFuncDecl) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    std::string output = print_ast(program.get());
    EXPECT_NE(output.find("FuncDecl"), std::string::npos);
    EXPECT_NE(output.find("name=main"), std::string::npos);
    EXPECT_NE(output.find("ret=int"), std::string::npos);
}

// Test: Print empty program
TEST_F(AstPrintingTest, PrintEmptyProgram) {
    std::string input = "FuncDecl name=main ret=void\n"
                        "  BlockStmt\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    std::string output = print_ast(program.get());
    // Should still print the FuncDecl
    EXPECT_NE(output.find("FuncDecl"), std::string::npos);
}

// Test: Print AstPrintingTest - null program
TEST_F(AstPrintingTest, PrintNullProgram) {
    std::string output = print_ast(nullptr);
    EXPECT_EQ(output, "");
}

// Test: Round-trip - parse, build, print, parse again
TEST_F(AstPrintingTest, RoundTrip) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=x type=int\n"
                        "      IntLiteral value=10\n"
                        "    ReturnStmt\n"
                        "      VarRef name=x\n";

    // First pass
    auto nodes1 = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> roots1;
    for (auto& r : *nodes1) roots1.push_back(r.get());
    auto program1 = build_ast_from_roots(roots1, ctx);

    // Print
    std::string printed = print_ast(program1.get());

    // The printed output starts with "Program" line, skip it for re-parse
    // Find the first FuncDecl line
    size_t func_pos = printed.find("FuncDecl");
    ASSERT_NE(func_pos, std::string::npos);
    std::string func_part = printed.substr(func_pos);

    // Second pass - parse the FuncDecl part only
    Context ctx2;
    auto nodes2 = parse_ast_format(func_part, ctx2);
    std::vector<ParsedNode*> roots2;
    for (auto& r : *nodes2) roots2.push_back(r.get());
    auto program2 = build_ast_from_roots(roots2, ctx2);

    // Compare
    std::string printed2 = print_ast(program2.get());
    EXPECT_EQ(printed, printed2);
}

// Test: Print AstPrintingTest::BinaryOp
TEST_F(AstPrintingTest, PrintBinaryOp) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      BinaryOp op=+ type=int\n"
                        "        IntLiteral value=1\n"
                        "        IntLiteral value=2\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    std::string output = print_ast(program.get());
    EXPECT_NE(output.find("BinaryOp"), std::string::npos);
    EXPECT_NE(output.find("op=+"), std::string::npos);
}

// Test: Print AstPrintingTest::IfStmt
TEST_F(AstPrintingTest, PrintIfStmt) {
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
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    std::string output = print_ast(program.get());
    EXPECT_NE(output.find("IfStmt"), std::string::npos);
    EXPECT_NE(output.find("ThenBlock"), std::string::npos);
    EXPECT_NE(output.find("ElseBlock"), std::string::npos);
}

// Test: Print AstPrintingTest::CallExpr
TEST_F(AstPrintingTest, PrintCallExpr) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ExprStmt\n"
                        "      CallExpr name=print\n"
                        "        StringLiteral value=\"hello\"\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    std::string output = print_ast(program.get());
    EXPECT_NE(output.find("CallExpr"), std::string::npos);
    EXPECT_NE(output.find("name=print"), std::string::npos);
}