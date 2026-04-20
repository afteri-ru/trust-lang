import trust;

#include "ast_format_parser.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace trust;

static std::unique_ptr<trust::Context> make_context() {
    auto ctx = std::make_unique<trust::Context>();
    ctx->diag().setOutput(&std::cerr);
    return ctx;
}

// Helper: parse AST, run semantic analysis, return TypeResolution
static std::unique_ptr<Program> parse_and_build(const std::string& input) {
    Context ctx;
    auto parsed = parse_ast_format(input, ctx);
    std::vector<ParsedNode*> roots;
    for (auto& r : *parsed) roots.push_back(r.get());
    return build_ast_from_roots(roots, ctx);
}

// Helper: run semantic analysis on a program
static TypeResolution run_semantic(Program* program, Context& ctx) {
    TypeResolution type_res;
    SymbolTable sym_table;
    SemanticAnalyzer sa(type_res, sym_table, ctx);
    sa.analyze(program);
    return type_res;
}

TEST(SemanticAnalyzerTest, IntLiteralType) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=x\n"
                        "      IntLiteral value=42\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto program = parse_and_build(input);
    auto tr = run_semantic(program.get(), *ctx.get());

    auto* func = static_cast<const FuncDecl*>(program->items[0].get());
    auto* var_decl = static_cast<const VarDecl*>(func->body->body[0].get());
    auto* int_lit = static_cast<const IntLiteral*>(var_decl->init.get());

    ASSERT_TRUE(tr.has_type(int_lit));
    auto t = tr.get_type(int_lit);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->kind, TypeKind::Int);
}

TEST(SemanticAnalyzerTest, StringLiteralType) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=msg\n"
                        "      StringLiteral value=\"hello\"\n";
    auto program = parse_and_build(input);
    auto type_res = run_semantic(program.get(), *ctx.get());

    auto* func = static_cast<const FuncDecl*>(program->items[0].get());
    auto* var_decl = static_cast<const VarDecl*>(func->body->body[0].get());
    auto* str_lit = static_cast<const StringLiteral*>(var_decl->init.get());

    ASSERT_TRUE(type_res.has_type(str_lit));
    auto t = type_res.get_type(str_lit);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->kind, TypeKind::String);
}

TEST(SemanticAnalyzerTest, BinaryOpComparisonType) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=flag\n"
                        "      BinaryOp op=>=\n"
                        "        IntLiteral value=10\n"
                        "        IntLiteral value=5\n";
    auto program = parse_and_build(input);
    auto type_res = run_semantic(program.get(), *ctx.get());

    auto* func = static_cast<const FuncDecl*>(program->items[0].get());
    auto* var_decl = static_cast<const VarDecl*>(func->body->body[0].get());
    auto* bin_op = static_cast<const BinaryOp*>(var_decl->init.get());

    auto t = type_res.get_type(bin_op);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->kind, TypeKind::Bool);
}

TEST(SemanticAnalyzerTest, BinaryOpArithmeticType) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=sum type=int\n"
                        "      BinaryOp op=+\n"
                        "        IntLiteral value=10\n"
                        "        IntLiteral value=5\n";
    auto program = parse_and_build(input);
    auto type_res = run_semantic(program.get(), *ctx.get());

    auto* func = static_cast<const FuncDecl*>(program->items[0].get());
    auto* var_decl = static_cast<const VarDecl*>(func->body->body[0].get());
    auto* bin_op = static_cast<const BinaryOp*>(var_decl->init.get());

    auto t = type_res.get_type(bin_op);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->kind, TypeKind::Int);
}

TEST(SemanticAnalyzerTest, EnumLiteralTypeResolved) {
    auto ctx = make_context();
    std::string input = "EnumDecl name=Color\n"
                        "  EnumMember name=Red value=1\n"
                        "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=c type=Color\n"
                        "      EnumLiteral enum=Color member=Red\n";
    auto program = parse_and_build(input);
    auto type_res = run_semantic(program.get(), *ctx.get());

    auto* var_decl = static_cast<const VarDecl*>(static_cast<const FuncDecl*>(program->items[1].get())->body->body[0].get());
    auto* enum_lit = static_cast<const EnumLiteral*>(var_decl->init.get());

    ASSERT_TRUE(type_res.has_type(enum_lit));
    auto t = type_res.get_type(enum_lit);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->kind, TypeKind::UserType);
    EXPECT_EQ(t->user_type_name, "Color");
}

TEST(SemanticAnalyzerTest, NestedBinaryOpType) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=result type=string\n"
                        "      BinaryOp op=+\n"
                        "        StringLiteral value=\"hello\"\n"
                        "        StringLiteral value=\" world\"\n";
    auto program = parse_and_build(input);
    auto type_res = run_semantic(program.get(), *ctx.get());

    auto* func = static_cast<const FuncDecl*>(program->items[0].get());
    auto* var_decl = static_cast<const VarDecl*>(func->body->body[0].get());
    auto* bin_op = static_cast<const BinaryOp*>(var_decl->init.get());
    auto* left = static_cast<const StringLiteral*>(bin_op->left.get());

    ASSERT_TRUE(type_res.has_type(left));
    auto t_left = type_res.get_type(left);
    ASSERT_TRUE(t_left.has_value());
    EXPECT_EQ(t_left->kind, TypeKind::String);
    ASSERT_TRUE(type_res.has_type(bin_op));
    auto t_bin = type_res.get_type(bin_op);
    ASSERT_TRUE(t_bin.has_value());
    EXPECT_EQ(t_bin->kind, TypeKind::String);
}

TEST(SemanticAnalyzerTest, CodeGenWithTypeResolution) {
    auto ctx = make_context();
    std::string input = "FuncDecl name=main ret=int\n"
                        "  BlockStmt\n"
                        "    VarDecl name=x\n"
                        "      IntLiteral value=42\n"
                        "    ReturnStmt\n"
                        "      IntLiteral value=0\n";
    auto program = parse_and_build(input);

    auto type_res = std::make_shared<TypeResolution>();
    SymbolTable sym_table;
    SemanticAnalyzer sa(*type_res, sym_table, *ctx.get());
    sa.analyze(program.get());

    CppGenerator gen;
    gen.set_type_resolution(type_res);
    std::string code = gen.generate(program.get());

    EXPECT_NE(code.find("int x = 42"), std::string::npos);
}