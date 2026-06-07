#include "utils/io.hpp"
#include "transpiler/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "semantic/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "syntax/term.h"
#include "syntax/term_types.h"
#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace trust {
namespace {

class TranspilerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
    }

    void TearDown() override { setErrs(m_prev_err); }

    std::ostream* m_prev_err = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
};

/// Test: generateToFile for int variable (x := 42 → std::any x = 42;)
TEST_F(TranspilerTest, GenerateToFileIntVar) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 9));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(name, std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"), false);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_output.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("std::any x = 42;") != std::string::npos);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: generateToFile for string variable (s := hello → std::any s = hello;)
TEST_F(TranspilerTest, GenerateToFileStringVar) {

    MapperFile input_file = m_ctx.source().add_source("test2.src", "s := hello;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 12));

    std::string name = "s";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(name, std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::StringLiteral, "hello"), false);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_str.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("std::any s = \"hello\";") != std::string::npos);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: generateToFile for typed variable x:Int32 := 42;
TEST_F(TranspilerTest, GenerateToFileTypedVar) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x:Int32 := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 15));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(name, std::move(term), std::make_shared<IdentType>("Int32"),
                                         std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"), false);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_output.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("int32_t x = 42;") != std::string::npos);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: generateToFile for type declaration MyInt ::= :Int32;
TEST_F(TranspilerTest, GenerateToFileTypeDecl) {

    MapperFile input_file = m_ctx.source().add_source("test2.src", "MyInt ::= :Int32;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 18));

    auto opTerm = Term::Create(TermID::SYMBOL, "::=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));

    const std::string leftName = "MyInt";
    MapperRange left_range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 6));
    auto leftTerm = Term::Create(TermID::NAME, leftName, left_range, parser::token_type::NAME);
    b->m_left = std::make_shared<IdentName>(leftName, std::move(leftTerm));

    const std::string rightName = "Int32";
    auto rightTerm = Term::Create(TermID::NAME, rightName, {}, parser::token_type::NAME);
    b->m_right = std::make_shared<IdentType>(rightName, std::move(rightTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_type.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("using MyInt = int32_t;") != std::string::npos);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with simple assignment (x = 5 → x = 5;)
TEST_F(TranspilerTest, GenerateExprStmtSimpleAssign) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x = 5;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 7));

    auto opTerm = Term::Create(TermID::SYMBOL, "=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_assign.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x = 5;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with compound addition (x += 3 → x += 3;)
TEST_F(TranspilerTest, GenerateExprStmtCompoundAdd) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x += 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto opTerm = Term::Create(TermID::SYMBOL, "+=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_add.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x += 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with compound subtraction (x -= 3 → x -= 3;)
TEST_F(TranspilerTest, GenerateExprStmtCompoundSub) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x -= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto opTerm = Term::Create(TermID::SYMBOL, "-=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_sub.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x -= 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with compound multiplication
TEST_F(TranspilerTest, GenerateExprStmtCompoundMul) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x *= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto opTerm = Term::Create(TermID::SYMBOL, "*=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_mul.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x *= 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with compound division
TEST_F(TranspilerTest, GenerateExprStmtCompoundDiv) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x /= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto opTerm = Term::Create(TermID::SYMBOL, "/=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_div.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x /= 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: expression statement with compound remainder
TEST_F(TranspilerTest, GenerateExprStmtCompoundRem) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x %= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto opTerm = Term::Create(TermID::SYMBOL, "%=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_rem.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x %= 3;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: integer division operator // → static_cast<int64_t>(x) / static_cast<int64_t>(y)
TEST_F(TranspilerTest, GenerateExprIntDiv) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x // 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto opTerm = Term::Create(TermID::SYMBOL, "//", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_intdiv.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("static_cast<int64_t>(x) / static_cast<int64_t>(3)") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: compound integer division //= → x = static_cast<int64_t>(x) / y
TEST_F(TranspilerTest, GenerateExprStmtCompoundIntDiv) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x //= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 9));

    auto opTerm = Term::Create(TermID::SYMBOL, "//=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_intdiv_assign.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x = static_cast<int64_t>(x) / static_cast<int64_t>(3)") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: function declaration with Int8 param, None return
TEST_F(TranspilerTest, GenerateFuncDeclInt8Param) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "%func(arg:Int8):None ::= { };", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 5));

    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>("%func", std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange param_range(m_ctx.source().makeLoc(input_file, 6), m_ctx.source().makeLoc(input_file, 9));
    auto paramTerm = Term::Create(TermID::NAME, "arg", param_range, parser::token_type::NAME);
    auto param = std::make_shared<ParamDecl>("arg", std::move(paramTerm), std::make_shared<IdentType>("Int8"));
    func->m_params->push_back(param);
    func->m_type = std::make_shared<IdentType>("None");
    func->m_body = std::vector<AstNodePtr>{};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq); // register function name, result ignored for codegen test
    }

    MapperFile out_idx = m_ctx.source().add_output("test_func.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("void func(int8_t arg)") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("{\n}") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: function declaration with multiple params and return type
TEST_F(TranspilerTest, GenerateFuncDeclTyped) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "%func(a:Int32, b:Byte):Float ::= { ++ 0.0. ++; };", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 5));
    MapperRange ret_range(m_ctx.source().makeLoc(input_file, 30), m_ctx.source().makeLoc(input_file, 45));

    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>("%func", std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange p1_range(m_ctx.source().makeLoc(input_file, 7), m_ctx.source().makeLoc(input_file, 8));
    auto p1Term = Term::Create(TermID::NAME, "a", p1_range, parser::token_type::NAME);
    auto p1 = std::make_shared<ParamDecl>("a", std::move(p1Term), std::make_shared<IdentType>("Int32"));
    func->m_params->push_back(p1);
    MapperRange p2_range(m_ctx.source().makeLoc(input_file, 15), m_ctx.source().makeLoc(input_file, 16));
    auto p2Term = Term::Create(TermID::NAME, "b", p2_range, parser::token_type::NAME);
    auto p2 = std::make_shared<ParamDecl>("b", std::move(p2Term), std::make_shared<IdentType>("Byte"));
    func->m_params->push_back(p2);
    func->m_type = std::make_shared<IdentType>("Float");

    auto retTerm = Term::Create(TermID::SYMBOL, "++", ret_range, parser::token_type::END);
    auto ret = std::make_shared<JumpStmt>(ParserToken::Kind::ReturnStmt, std::move(retTerm));
    auto val_ptr = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0", ret_range);
    ret->m_value = std::move(val_ptr);
    func->m_body = std::vector<AstNodePtr>{};
    func->m_body->push_back(ret);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq); // register function name, result ignored for codegen test
    }

    MapperFile out_idx = m_ctx.source().add_output("test_func2.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    // Float type is not registered, so it stays as "Float" (no resolution)
    EXPECT_TRUE(result.find("Float func(int32_t a, uint8_t b)") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("return 0;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: forward function declaration (no body)
TEST_F(TranspilerTest, GenerateFuncDeclForward) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "%forward_func():Void ::= ;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 13));

    auto funcTerm = Term::Create(TermID::NAME, "%forward_func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>("%forward_func", std::move(funcTerm));
    func->m_type = std::make_shared<IdentType>("Void");
    // no m_body = forward decl

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq); // register function name, result ignored for codegen test
    }

    MapperFile out_idx = m_ctx.source().add_output("test_forward.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("void forward_func()") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find(";\n") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: throw statement (-- expr -- → throw expr;)
TEST_F(TranspilerTest, GenerateThrowStmt) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "-- value --;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 14));

    auto opTerm = Term::Create(TermID::SYMBOL, "--", range, parser::token_type::END);
    auto stmt = std::make_shared<JumpStmt>(ParserToken::Kind::ThrowStmt, std::move(opTerm));
    stmt->m_value = std::make_shared<IdentName>("value");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(stmt));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_throw.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("throw value;") != std::string::npos) << "result: " << result;
}

/// Test: throw void (-- -- → throw;)
TEST_F(TranspilerTest, GenerateThrowVoid) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "-- --;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 7));

    auto opTerm = Term::Create(TermID::SYMBOL, "--", range, parser::token_type::END);
    auto stmt = std::make_shared<JumpStmt>(ParserToken::Kind::ThrowStmt, std::move(opTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(stmt));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_throw_void.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("throw;") != std::string::npos) << "result: " << result;
}

/// Test: standalone literal in sequence (42 → 42;)
TEST_F(TranspilerTest, GenerateStandaloneLiteral) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 4));

    auto litTerm = Term::Create(TermID::INTEGER, "42", range, parser::token_type::END);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42", std::move(litTerm)));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_literal.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("42;") != std::string::npos) << "result: " << result;
}

/// Test: standalone string literal ("hello" → "hello";)
TEST_F(TranspilerTest, GenerateStandaloneStringLiteral) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "\"hello\";", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 9));

    auto litTerm = Term::Create(TermID::STRCHAR, "hello", range, parser::token_type::END);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::make_shared<Literal>(ParserToken::Kind::StringLiteral, "hello", std::move(litTerm)));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_str_literal.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("\"hello\";") != std::string::npos) << "result: " << result;
}

/// Test: exports() contains variable declarations and function declarations
TEST_F(TranspilerTest, GenerateExports) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42; %foo():None ::= { };", true);
    MapperRange var_range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 9));
    MapperRange func_range(m_ctx.source().makeLoc(input_file, 10), m_ctx.source().makeLoc(input_file, 28));

    std::string varName = "x";
    auto varTerm = Term::Create(TermID::NAME, varName, var_range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(varName, std::move(varTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"), false);

    auto funcTerm = Term::Create(TermID::NAME, "%foo", func_range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>("%foo", std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_type = std::make_shared<IdentType>("None");
    func->m_body = std::vector<AstNodePtr>{};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(func));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_exports.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    const auto& exports = gen.exports();
    ASSERT_EQ(exports.size(), 2u);
    EXPECT_EQ(exports[0].trustName, "x");
    EXPECT_EQ(exports[0].cppName, "x");
    EXPECT_EQ(exports[1].trustName, "%foo");
    EXPECT_EQ(exports[1].cppName, "foo");
}

/// Test: Sequence node walks body (nested declarations)
TEST_F(TranspilerTest, GenerateSequenceWalk) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "{ x := 1; y := 2; }", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 22));

    MapperRange range_x(m_ctx.source().makeLoc(input_file, 3), m_ctx.source().makeLoc(input_file, 10));
    MapperRange range_y(m_ctx.source().makeLoc(input_file, 12), m_ctx.source().makeLoc(input_file, 19));

    auto seqTerm = Term::Create(TermID::SEQUENCE, "", range, parser::token_type::END);
    auto seq_node = std::make_shared<Sequence>(ParserToken::Kind::sequence, "", std::move(seqTerm));

    std::string xName = "x";
    auto xTerm = Term::Create(TermID::NAME, xName, range_x, parser::token_type::NAME);
    seq_node->m_body.push_back(
        std::make_shared<VarDecl>(xName, std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"), false));

    std::string yName = "y";
    auto yTerm = Term::Create(TermID::NAME, yName, range_y, parser::token_type::NAME);
    seq_node->m_body.push_back(
        std::make_shared<VarDecl>(yName, std::move(yTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"), false));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(seq_node));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_seq.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("std::any x = 1;") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("std::any y = 2;") != std::string::npos) << "result: " << result;
}

/// Test: string literal with embedded quote is escaped ("a\"b" → "a\"b")
TEST_F(TranspilerTest, GenerateStringWithEscapedQuote) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "s := \"a\\\"b\";", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 15));

    std::string name = "s";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(name, std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::StringLiteral, "a\\\"b"), false);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_esc.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("std::any s = \"a\\\"b\";") != std::string::npos) << "result: " << result;
}

/// Test: mutable flag does not change generated code (current behaviour)
TEST_F(TranspilerTest, GenerateMutableVarDecl) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42 mut;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 13));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(name, std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"), true);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_mut.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("std::any x = 42;") != std::string::npos) << "result: " << result;
}

/// Test: TypeName as expression (x := :Int32 → std::any x = Int32;)
TEST_F(TranspilerTest, GenerateTypeNameExpr) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := :Int32;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 13));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(name, std::move(term), nullptr, std::make_shared<IdentType>("Int32"), false);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticAnalyzer analyzer(m_ctx);
        analyzer.analyze(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_typename_expr.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("std::any x = Int32;") != std::string::npos) << "result: " << result;
}

/// Test: addNameMapping for variable name is queryable via getCppName.
TEST_F(TranspilerTest, NameMapping_VarDecl) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));
    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(name, std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"), false);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_nm_var.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto cppName = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 1)), "x");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "x");
    // The cpp range must point at the variable name 'x' in the generated output.
    EXPECT_EQ(reader->getText(cppName->rangeMap.to), "x");
}

/// Test: addNameMapping for type name is queryable via getCppName.
TEST_F(TranspilerTest, NameMapping_TypeDecl) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "MyInt ::= :Int32;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 18));
    auto opTerm = Term::Create(TermID::SYMBOL, "::=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::move(opTerm));
    const std::string leftName = "MyInt";
    MapperRange left_range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 6));
    auto leftTerm = Term::Create(TermID::NAME, leftName, left_range, parser::token_type::NAME);
    b->m_left = std::make_shared<IdentName>(leftName, std::move(leftTerm));
    const std::string rightName = "Int32";
    auto rightTerm = Term::Create(TermID::NAME, rightName, {}, parser::token_type::NAME);
    b->m_right = std::make_shared<IdentType>(rightName, std::move(rightTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(b));

    SemanticAnalyzer analyzer(m_ctx);
    ASSERT_TRUE(analyzer.analyze(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_nm_type.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto cppName = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 1)), "MyInt");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "MyInt");
    // The cpp range must point at the type name 'MyInt' in the generated output.
    EXPECT_EQ(reader->getText(cppName->rangeMap.to), "MyInt");
}

/// Test: addNameMapping for function name is queryable via getCppName.
TEST_F(TranspilerTest, NameMapping_FuncDeclName) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%func():Void ::= ;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 6));
    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>("%func", std::move(funcTerm));
    func->m_type = std::make_shared<IdentType>("Void");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticAnalyzer analyzer(m_ctx);
    analyzer.analyze(seq);

    MapperFile out_idx = m_ctx.source().add_output("test_nm_func.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_NE(result.find("void func();"), std::string::npos) << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto cppName = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 1)), "%func");
    ASSERT_TRUE(cppName.has_value());
    EXPECT_EQ(cppName->toName, "func");
    // The cpp range must point at the function name 'func' in the generated output.
    EXPECT_EQ(reader->getText(cppName->rangeMap.to), "func");
}

/// Test: addNameMapping for function parameters is queryable via getCppName.
TEST_F(TranspilerTest, NameMapping_FuncParams) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%func(a:Int32,b:Int32):Void ::= ;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 6));
    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>("%func", std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange a_range(m_ctx.source().makeLoc(input_file, 7), m_ctx.source().makeLoc(input_file, 8));
    auto aTerm = Term::Create(TermID::NAME, "a", a_range, parser::token_type::NAME);
    auto pa = std::make_shared<ParamDecl>("a", std::move(aTerm), std::make_shared<IdentType>("Int32"));
    func->m_params->push_back(pa);
    MapperRange b_range(m_ctx.source().makeLoc(input_file, 15), m_ctx.source().makeLoc(input_file, 16));
    auto bTerm = Term::Create(TermID::NAME, "b", b_range, parser::token_type::NAME);
    auto pb = std::make_shared<ParamDecl>("b", std::move(bTerm), std::make_shared<IdentType>("Int32"));
    func->m_params->push_back(pb);
    func->m_type = std::make_shared<IdentType>("Void");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticAnalyzer analyzer(m_ctx);
    analyzer.analyze(seq);

    MapperFile out_idx = m_ctx.source().add_output("test_nm_params.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_NE(result.find("void func(int32_t a, int32_t b);"), std::string::npos) << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
    auto aCpp = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 7)), "a");
    ASSERT_TRUE(aCpp.has_value());
    EXPECT_EQ(aCpp->toName, "a");
    EXPECT_EQ(reader->getText(aCpp->rangeMap.to), "a");

    auto bCpp = reader->getCppName(static_cast<ReaderLocation>(m_ctx.source().makeLoc(input_file, 15)), "b");
    ASSERT_TRUE(bCpp.has_value());
    EXPECT_EQ(bCpp->toName, "b");
    EXPECT_EQ(reader->getText(bCpp->rangeMap.to), "b");
}

} // namespace
} // namespace trust