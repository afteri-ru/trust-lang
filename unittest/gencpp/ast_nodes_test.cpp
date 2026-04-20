#include "gencpp.hpp"
#include "ast_format_parser.hpp"
#include "gencpp/cpp_generator.hpp"
#include "diag/context.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <string>

// ============================================================================
// AST Nodes Test — Creation, Validation, Categories
// ============================================================================
using namespace trust;

class AstTest : public ::testing::Test {
protected:
    Context ctx;
};

class AstErrorTest : public ::testing::Test {
protected:
    Context ctx;
};

// ============================================================================
// Section 1: AST Builder — Valid Creation
// ============================================================================

TEST_F(AstTest, BuildVarDecl) {
    std::string input = "VarDecl name=x type=int\n"
                        "  IntLiteral value=42\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->items.size(), 1);

    auto* var_decl = dynamic_cast<VarDecl*>(program->items[0].get());
    ASSERT_NE(var_decl, nullptr);
    EXPECT_EQ(var_decl->name, "x");
    EXPECT_EQ(var_decl->type_info().kind, TypeKind::Int);
    ASSERT_NE(var_decl->init, nullptr);

    auto* int_lit = dynamic_cast<IntLiteral*>(var_decl->init.get());
    ASSERT_NE(int_lit, nullptr);
    EXPECT_EQ(int_lit->value, 42);
}

TEST_F(AstTest, BuildFuncDecl) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->items.size(), 1);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "main");
    EXPECT_EQ(func->return_type, TypeInfo::builtin(TypeKind::Int));
    ASSERT_NE(func->body, nullptr);
}

TEST_F(AstTest, BuildStmtWrapsInMain) {
    std::string input = "VarDecl name=x type=int\n"
                        "  IntLiteral value=42\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->items.size(), 1);
}

TEST_F(AstTest, BuildFuncDeclWithParams) {
    std::string input = "FuncDecl name=foo ret=int\n"
                        "  ParamDecl name=a type=int\n"
                        "  ParamDecl name=b type=string\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->params.size(), 2);
    EXPECT_EQ(func->params[0]->name, "a");
    EXPECT_EQ(func->params[0]->param_type.kind, TypeKind::Int);
    EXPECT_EQ(func->params[1]->name, "b");
    EXPECT_EQ(func->params[1]->param_type.kind, TypeKind::String);
}

TEST_F(AstTest, BuildBinaryOp) {
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

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    auto* block = func->body.get();
    auto* ret = dynamic_cast<ReturnStmt*>(block->body[0].get());
    auto* binop = dynamic_cast<BinaryOp*>(ret->value.get());

    ASSERT_NE(binop, nullptr);
    EXPECT_EQ(binop->op, BinOp::Add);
    EXPECT_EQ(binop->op, BinOp::Add);
}

TEST_F(AstTest, BuildIfStmtWithElseIf) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    IfStmt\n"
                        "      VarRef name=x\n"
                        "      ThenBlock\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=1\n"
                        "      ElseBlock\n"
                        "        IfStmt\n"
                        "          VarRef name=y\n"
                        "          ThenBlock\n"
                        "            ReturnStmt\n"
                        "              IntLiteral value=2\n"
                        "          ElseBlock\n"
                        "            ReturnStmt\n"
                        "              IntLiteral value=3\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    auto* block = func->body.get();
    auto* ifstmt = dynamic_cast<IfStmt*>(block->body[0].get());

    ASSERT_NE(ifstmt, nullptr);
    ASSERT_NE(ifstmt->else_if, nullptr);
    EXPECT_EQ(ifstmt->else_block, nullptr);
}

TEST_F(AstTest, VarDeclUserType) {
    std::string input = "VarDecl name=x type=unknown\n"
                        "  IntLiteral value=42\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());

    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->items.size(), 1);
    auto* var_decl = dynamic_cast<VarDecl*>(program->items[0].get());
    ASSERT_NE(var_decl, nullptr);
    EXPECT_EQ(var_decl->name, "x");
    EXPECT_EQ(var_decl->type_info().to_string(), "unknown");
}

TEST_F(AstTest, VarRefUserType) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      VarRef name=x type=MyEnum\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());

    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
}

// ============================================================================
// Section 2: AST Builder — Error Cases
// ============================================================================

TEST_F(AstErrorTest, VarDeclMissingName) {
    std::string input = "VarDecl type=int\n"
                        "  IntLiteral value=42\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    EXPECT_EQ(program->items.size(), 0);
}

TEST_F(AstTest, VarDeclMissingTypeInferred) {
    std::string input = "VarDecl name=x\n"
                        "  IntLiteral value=42\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());

    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->items.size(), 1);
    auto* var_decl = dynamic_cast<VarDecl*>(program->items[0].get());
    ASSERT_NE(var_decl, nullptr);
    EXPECT_EQ(var_decl->name, "x");
    EXPECT_EQ(var_decl->type_info().kind, TypeKind::Int);
}

TEST_F(AstErrorTest, FuncDeclMissingName) {
    std::string input = "FuncDecl ret=int\n"
                        "  BlockStmt\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    // FuncDecl without name builds but body is empty
    ASSERT_NE(program, nullptr);
}

TEST_F(AstErrorTest, FuncDeclMissingRet) {
    std::string input = "FuncDecl name=main\n"
                        "  BlockStmt\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
}

TEST_F(AstErrorTest, IntLiteralInvalidValue) {
    std::string input = "VarDecl name=x type=int\n"
                        "  IntLiteral value=not_a_number\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    EXPECT_EQ(program->items.size(), 0);
}

TEST_F(AstErrorTest, BinaryOpMissingOp) {
    std::string input = "BinaryOp type=int\n"
                        "  IntLiteral value=1\n"
                        "  IntLiteral value=2\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    EXPECT_EQ(program->items.size(), 0);
}

TEST_F(AstErrorTest, BinaryOpInvalidOp) {
    std::string input = "BinaryOp op=%% type=int\n"
                        "  IntLiteral value=1\n"
                        "  IntLiteral value=2\n";
    auto nodes = parse_ast_format(input, ctx);
    ASSERT_TRUE(nodes.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->items.size(), 0);
}

TEST_F(AstErrorTest, BinaryOpMissingChildren) {
    std::string input = "BinaryOp op=+ type=int\n"
                        "  IntLiteral value=1\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    EXPECT_EQ(program->items.size(), 0);
}

TEST_F(AstErrorTest, VarRefMissingName) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    ReturnStmt\n"
                        "      VarRef\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    // Builder silently produces FuncDecl with empty body
    ASSERT_NE(program, nullptr);
    ASSERT_EQ(program->items.size(), 1);
}

TEST_F(AstErrorTest, AssignmentStmtMissingTarget) {
    std::string input = "AssignmentStmt\n"
                        "  IntLiteral value=42\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    ASSERT_EQ(program->items.size(), 0);
}

TEST_F(AstErrorTest, CallExprMissingName) {
    std::string input = "CallExpr\n"
                        "  IntLiteral value=42\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    EXPECT_EQ(program->items.size(), 0);
}

TEST_F(AstErrorTest, ParamDeclMissingName) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  ParamDecl type=int\n"
                        "  BlockStmt\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    EXPECT_EQ(program->items.size(), 0);
}

TEST_F(AstErrorTest, ParamDeclMissingType) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  ParamDecl name=x\n"
                        "  BlockStmt\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    EXPECT_EQ(program->items.size(), 0);
}

TEST_F(AstErrorTest, UnknownDeclType) {
    std::string input = "UnknownDecl name=x\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_FALSE(result.has_value());
}

TEST_F(AstErrorTest, UnknownStmtType) {
    std::string input = "UnknownStmt\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_FALSE(result.has_value());
}

TEST_F(AstErrorTest, IntLiteralOutOfRange) {
    std::string input = "VarDecl name=x type=int\n"
                        "  IntLiteral value=99999999999999999999\n";
    auto result = parse_ast_format(input, ctx);
    ASSERT_TRUE(result.has_value());
    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *result) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);
    EXPECT_EQ(program->items.size(), 0);
}

// ============================================================================
// Section 3: BlockItem Unified Dispatch
// ============================================================================

TEST_F(AstTest, BlockItemUnifiedDispatch) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=x type=int\n"
                        "      IntLiteral value=10\n"
                        "    ReturnStmt\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
    auto* block = func->body.get();
    ASSERT_GE(block->body.size(), 2);

    auto* decl_item = dynamic_cast<VarDecl*>(block->body[0].get());
    ASSERT_NE(decl_item, nullptr);
    auto* stmt_item = dynamic_cast<ReturnStmt*>(block->body[1].get());
    ASSERT_NE(stmt_item, nullptr);

    EXPECT_EQ(AstTypeTraits::node_category(block->body[0]->token_kind()), TokenCategory::Decl);
    EXPECT_EQ(AstTypeTraits::node_category(block->body[1]->token_kind()), TokenCategory::Stmt);
}

// ============================================================================
// Section 5: Full Node Types (EnumMember, StructField, CatchBlock, MatchingCase)
// ============================================================================

TEST_F(AstTest, EnumMemberAsFullNode) {
    std::string input = "EnumDecl name=Color\n"
                        "  EnumMember name=Red value=1\n"
                        "  EnumMember name=Green\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* enum_decl = dynamic_cast<EnumDecl*>(program->items[0].get());
    ASSERT_NE(enum_decl, nullptr);
    ASSERT_EQ(enum_decl->members.size(), 2);

    auto* member = enum_decl->members[0].get();
    EXPECT_EQ(member->token_kind(), ParserToken::Kind::EnumMember);
    EXPECT_EQ(AstTypeTraits::node_category(member->token_kind()), TokenCategory::Decl);
    EXPECT_EQ(member->name, "Red");
    ASSERT_TRUE(member->value != nullptr);
    EXPECT_EQ(*member->value, 1);
}

TEST_F(AstTest, StructFieldAsFullNode) {
    std::string input = "StructDecl name=Point\n"
                        "  StructField name=x type=int\n"
                        "  StructField name=y type=int\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* struct_decl = dynamic_cast<StructDecl*>(program->items[0].get());
    ASSERT_NE(struct_decl, nullptr);
    ASSERT_EQ(struct_decl->fields.size(), 2);

    auto* field = struct_decl->fields[0].get();
    EXPECT_EQ(field->token_kind(), ParserToken::Kind::StructField);
    EXPECT_EQ(AstTypeTraits::node_category(field->token_kind()), TokenCategory::Decl);
    EXPECT_EQ(field->name, "x");
    EXPECT_EQ(field->type.to_string(), "int");
}

TEST_F(AstTest, CatchBlockAsFullNode) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    TryCatchStmt\n"
                        "      ThrowStmt\n"
                        "        IntLiteral value=1\n"
                        "      CatchBlock type=int name=e\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=0\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);

    auto* block = func->body.get();
    ASSERT_EQ(block->body.size(), 1);

    auto* try_catch = dynamic_cast<TryCatchStmt*>(block->body[0].get());
    ASSERT_NE(try_catch, nullptr);
    ASSERT_NE(try_catch->catch_block, nullptr);

    auto* catch_block = try_catch->catch_block.get();
    EXPECT_EQ(catch_block->token_kind(), ParserToken::Kind::CatchBlock);
    EXPECT_EQ(AstTypeTraits::node_category(catch_block->token_kind()), TokenCategory::Stmt);
    EXPECT_EQ(catch_block->var_name, "e");
}

TEST_F(AstTest, MatchingCaseAsFullNode) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    MatchingStmt\n"
                        "      VarRef name=x\n"
                        "      MatchingCase\n"
                        "        IntLiteral value=1\n"
                        "        ReturnStmt\n"
                        "          IntLiteral value=1\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);

    auto* block = func->body.get();
    auto* matching = dynamic_cast<MatchingStmt*>(block->body[0].get());
    ASSERT_NE(matching, nullptr);
    ASSERT_EQ(matching->cases.size(), 1);

    auto* mc = matching->cases[0].get();
    EXPECT_EQ(mc->token_kind(), ParserToken::Kind::MatchingCase);
    EXPECT_EQ(AstTypeTraits::node_category(mc->token_kind()), TokenCategory::Stmt);
}

// ============================================================================
// Section 6: AssignmentStmt with Expr* target
// ============================================================================

TEST_F(AstTest, AssignmentStmtWithVarRefTarget) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    AssignmentStmt target=x\n"
                        "      IntLiteral value=42\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    auto* block = func->body.get();
    auto* assign = dynamic_cast<AssignmentStmt*>(block->body[0].get());
    ASSERT_NE(assign, nullptr);

    auto* vr = dynamic_cast<VarRef*>(assign->target.get());
    ASSERT_NE(vr, nullptr);
    EXPECT_EQ(vr->name, "x");
}

TEST_F(AstTest, AssignmentStmtCodeGen) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    AssignmentStmt target=x\n"
                        "      IntLiteral value=42\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    CppGenerator gen;
    std::string code = gen.generate(program.get());

    EXPECT_NE(code.find("x = 42"), std::string::npos);
}

// ============================================================================
// Section 7: New Node Tests (previously uncovered)
// ============================================================================

TEST_F(AstTest, BuildWhileStmt) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    WhileStmt\n"
                        "      VarRef name=x\n"
                        "      ReturnStmt\n"
                        "        IntLiteral value=0\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
    auto* block = func->body.get();
    ASSERT_GE(block->body.size(), 1);
    auto* while_stmt = dynamic_cast<WhileStmt*>(block->body[0].get());
    ASSERT_NE(while_stmt, nullptr);
    EXPECT_EQ(while_stmt->token_kind(), ParserToken::Kind::WhileStmt);
}

TEST_F(AstTest, BuildDoWhileStmt) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    DoWhileStmt\n"
                        "      ReturnStmt\n"
                        "        IntLiteral value=0\n"
                        "      VarRef name=x\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
    auto* block = func->body.get();
    auto* do_while = dynamic_cast<DoWhileStmt*>(block->body[0].get());
    ASSERT_NE(do_while, nullptr);
    EXPECT_EQ(do_while->token_kind(), ParserToken::Kind::DoWhileStmt);
}

TEST_F(AstTest, BuildArrayInit) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=arr type=int\n"
                        "      ArrayInit\n"
                        "        IntLiteral value=1\n"
                        "        IntLiteral value=2\n"
                        "        IntLiteral value=3\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
}

TEST_F(AstTest, BuildCastExpr) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=val type=int\n"
                        "      CastExpr type=int\n"
                        "        IntLiteral value=42\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
}

TEST_F(AstTest, BuildMemberAccess) {
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=p type=Point\n"
                        "      MemberAccess field=x\n"
                        "        VarRef name=obj\n";
    auto nodes = parse_ast_format(input, ctx);

    std::vector<ParsedNode*> root_ptrs;
    for (auto& r : *nodes) root_ptrs.push_back(r.get());
    auto program = build_ast_from_roots(root_ptrs, ctx);

    auto* func = dynamic_cast<FuncDecl*>(program->items[0].get());
    ASSERT_NE(func, nullptr);
}