#include "ast_format_parser.hpp"
#include "gencpp/ast_printer.hpp"
#include "gencpp/cpp_generator.hpp"
#include "diag/context.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace trust;

class LoopControlTest : public ::testing::Test {
protected:
    Context ctx;
};

TEST_F(LoopControlTest, BreakStmt) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    WhileStmt\n"
                        "      VarRef name=x\n"
                        "      BreakStmt\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, ContinueStmt) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    WhileStmt\n"
                        "      VarRef name=x\n"
                        "      ContinueStmt\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, WhileElseBlock) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    WhileStmt\n"
                        "      VarRef name=x\n"
                        "      ContinueStmt\n"
                        "      WhileElseBlock\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=0\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, DoWhileBreak) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      BreakStmt\n"
                        "      VarRef name=x\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, NestedWhileBreak) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    WhileStmt\n"
                        "      VarRef name=x\n"
                        "      WhileStmt\n"
                        "        VarRef name=y\n"
                        "        BreakStmt\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, NestedDoWhileBreak) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      DoWhileStmt\n"
                        "        BreakStmt\n"
                        "        VarRef name=inner\n"
                        "      VarRef name=outer\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, ContinueOuterDowhile) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      DoWhileStmt\n"
                        "        ContinueStmt\n"
                        "        VarRef name=inner\n"
                        "      VarRef name=outer\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, WhileElseBreak) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    WhileStmt\n"
                        "      VarRef name=x\n"
                        "      BreakStmt\n"
                        "      WhileElseBlock\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=0\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, WhileElseNested) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    WhileStmt\n"
                        "      VarRef name=x\n"
                        "      WhileStmt\n"
                        "        VarRef name=y\n"
                        "        BreakStmt\n"
                        "      WhileElseBlock\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=0\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, DoWhileNested) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      DoWhileStmt\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=0\n"
                        "        VarRef name=inner\n"
                        "      VarRef name=outer\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, BreakContinueDeep) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      DoWhileStmt\n"
                        "        DoWhileStmt\n"
                        "          BreakStmt\n"
                        "          VarRef name=deep\n"
                        "        VarRef name=mid\n"
                        "      VarRef name=top\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, DoWhileNestedPure) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      DoWhileStmt\n"
                        "        DoWhileStmt\n"
                        "          ContinueStmt\n"
                        "          VarRef name=deep\n"
                        "        VarRef name=mid\n"
                        "      VarRef name=top\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, DoWhileTripleNested) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      DoWhileStmt\n"
                        "        DoWhileStmt\n"
                        "          ReturnStmt\n"
                        "            IntLiteral value=0\n"
                        "          VarRef name=deep\n"
                        "        VarRef name=mid\n"
                        "      VarRef name=top\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, DoWhileBreakContinue) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      BreakStmt\n"
                        "      ContinueStmt\n"
                        "      VarRef name=x\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(LoopControlTest, ArrayDispatch) {
    std::string input = "VarDecl name=arr type=int\n"
                        "  ArrayInit type=int\n"
                        "    IntLiteral value=1\n"
                        "    IntLiteral value=2\n";
    auto nodes = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}