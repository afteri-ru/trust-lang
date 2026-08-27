#include "transpiler/transpiler_test_fixture.hpp"

namespace trust {
TEST_F(TranspilerTest, GenerateSemicolonStmtSimpleAssign) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x = 5;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 15));

    auto opTerm = Term::Create(TermID::ASSIGN, "=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_assign.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x = 5;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: call expression as var initializer (x := foo(1, 2) → std::any x = foo(1, 2);)
TEST_F(TranspilerTest, GenerateCallExprInInitializer) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "foo := 0; x := foo(1, 2);", true);
    MapperRange fooRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));
    MapperRange xRange(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 22));

    // foo := 0 - чтобы вызов foo(...) имел объявление (semantic lookup).
    auto fooTerm = Term::Create(TermID::NAME, "foo", fooRange, parser::token_type::NAME);
    auto fooVar = std::make_shared<VarDecl>(std::move(fooTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0"));

    // x := foo(1, 2) - инициализатор - CallExpr.
    auto callee = std::make_shared<IdentName>("foo");
    auto callTerm = Term::Create(TermID::NAME, "foo", xRange, parser::token_type::NAME);
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::move(callee));
    call->m_args = std::vector<AstNodePtr>{
        std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"),
        std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"),
    };

    auto xTerm = Term::Create(TermID::NAME, "x", xRange, parser::token_type::NAME);
    auto xVar = std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::move(call));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fooVar));
    seq.push_back(std::move(xVar));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_call.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("foo(1, 2)") != std::string::npos) << "result: " << result;
}
/// Test: expression statement with compound addition (x += 3 → x += 3;)

TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundAdd) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x += 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "+=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

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
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundSub) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x -= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "-=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

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
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundMul) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x *= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "*=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

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
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundDiv) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x /= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "/=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

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
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundRem) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x %= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::ASSIGN, "%=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

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

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x // 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 16));

    auto opTerm = Term::Create(TermID::OP_MATH, "//", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_intdiv.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("static_cast<int64_t>(c_x) / static_cast<int64_t>(3)") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: compound integer division //= → x = static_cast<int64_t>(x) / y
TEST_F(TranspilerTest, GenerateSemicolonStmtCompoundIntDiv) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 0; x //= 3;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 9), m_ctx.source().makeLoc(input_file, 17));

    auto opTerm = Term::Create(TermID::ASSIGN, "//=", range, parser::token_type::END);
    auto b = std::make_shared<Binary>(ParserToken::Kind::AssignOp, std::move(opTerm));
    b->m_left = std::make_shared<IdentName>("x");
    b->m_right = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "3");
    std::vector<AstNodePtr> seq;
    auto xTerm =
        Term::Create(TermID::NAME, "x", MapperRange(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8)), parser::token_type::NAME);
    seq.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0")));
    seq.push_back(std::move(b));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    MapperFile out_idx = m_ctx.source().add_output("test_intdiv_assign.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("x = static_cast<int64_t>(c_x) / static_cast<int64_t>(3)") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: function declaration with Int8 param, Void return
TEST_F(TranspilerTest, GenerateFuncDeclInt8Param) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "%func(arg:Int8):Void := { };", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 5));

    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange param_range(m_ctx.source().makeLoc(input_file, 6), m_ctx.source().makeLoc(input_file, 9));
    auto paramTerm = Term::Create(TermID::NAME, "arg", param_range, parser::token_type::NAME);
    auto param = std::make_shared<ArgNode>(std::move(paramTerm), std::make_shared<IdentType>("Int8"));
    func->m_params->push_back(param);
    func->m_type = std::make_shared<IdentType>("None");
    func->m_body = std::vector<AstNodePtr>{};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq); // register function name, result ignored for codegen test
    }

    MapperFile out_idx = m_ctx.source().add_output("test_func.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("void func(int8_t c_arg)") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("{\n}") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: function declaration with multiple params and return type
TEST_F(TranspilerTest, GenerateFuncDeclTyped) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "%func(a:Int32, b:Byte):Int64 := { ++ 0.0. ++; };", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 5));
    MapperRange ret_range(m_ctx.source().makeLoc(input_file, 30), m_ctx.source().makeLoc(input_file, 45));

    auto funcTerm = Term::Create(TermID::NAME, "%func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange p1_range(m_ctx.source().makeLoc(input_file, 7), m_ctx.source().makeLoc(input_file, 8));
    auto p1Term = Term::Create(TermID::NAME, "a", p1_range, parser::token_type::NAME);
    auto p1 = std::make_shared<ArgNode>(std::move(p1Term), std::make_shared<IdentType>("Int32"));
    func->m_params->push_back(p1);
    MapperRange p2_range(m_ctx.source().makeLoc(input_file, 7), m_ctx.source().makeLoc(input_file, 8));
    auto p2Term = Term::Create(TermID::NAME, "b", p2_range, parser::token_type::NAME);
    auto p2 = std::make_shared<ArgNode>(std::move(p2Term), std::make_shared<IdentType>("Byte"));
    func->m_params->push_back(p2);
    func->m_type = std::make_shared<IdentType>("Int64");

    auto retTerm = Term::Create(TermID::NAME, "++", ret_range, parser::token_type::END);
    auto ret = std::make_shared<JumpStmt>(ParserToken::Kind::ReturnStmt, std::move(retTerm));
    auto val_ptr = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "0");
    ret->m_value = std::move(val_ptr);
    func->m_body = std::vector<AstNodePtr>{};
    func->m_body->push_back(ret);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq); // register function name, result ignored for codegen test
    }

    MapperFile out_idx = m_ctx.source().add_output("test_func2.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    // Возврат - зарегистрированный тип Int64 → каноническое C++-имя int64_t.
    EXPECT_TRUE(result.find("int64_t func(int32_t c_a, uint8_t c_b)") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("return 0;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: forward function declaration (no body)
TEST_F(TranspilerTest, GenerateFuncDeclForward) {

    MapperFile input_file = m_ctx.source().add_source("test.src", "%forward_func():Void := ;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 13));

    auto funcTerm = Term::Create(TermID::NAME, "%forward_func", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_type = std::make_shared<IdentType>("Void");
    // no m_body = forward decl

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq); // register function name, result ignored for codegen test
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

/// Test: forward function declaration with Int32 return and params → декларация + #include <cstdint>
TEST_F(TranspilerTest, GenerateForwardFuncDeclInt32) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%add(a:Int32, b:Int32):Int32 := ...;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 5));

    auto funcTerm = Term::Create(TermID::NAME, "%add", range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    MapperRange p1(m_ctx.source().makeLoc(input_file, 7), m_ctx.source().makeLoc(input_file, 8));
    func->m_params->push_back(
        std::make_shared<ArgNode>(std::move(Term::Create(TermID::NAME, "a", p1, parser::token_type::NAME)), std::make_shared<IdentType>("Int32")));
    MapperRange p2(m_ctx.source().makeLoc(input_file, 10), m_ctx.source().makeLoc(input_file, 11));
    func->m_params->push_back(
        std::make_shared<ArgNode>(std::move(Term::Create(TermID::NAME, "b", p2, parser::token_type::NAME)), std::make_shared<IdentType>("Int32")));
    func->m_type = std::make_shared<IdentType>("Int32");
    // no m_body = forward decl

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        ASSERT_TRUE(runner.run(seq));
    }

    MapperFile out_idx = m_ctx.source().add_output("test_forward_int32.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("int32_t add(int32_t c_a, int32_t c_b);") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("#include <cstdint>") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: forward variable declaration (x:Int32 := ...; → extern int32_t c_x;)
TEST_F(TranspilerTest, GenerateForwardVarDecl) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x:Int32 := ...;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));

    auto nameTerm = Term::Create(TermID::NAME, "x", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(nameTerm), std::make_shared<IdentType>("Int32"), nullptr);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticPassRunner runner(m_ctx);
        ASSERT_TRUE(runner.run(seq));
    }

    MapperFile out_idx = m_ctx.source().add_output("test_fwdvar.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("extern int32_t c_x;") != std::string::npos) << "result: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: native variable `%x:Int32 := 5;` → манглинг `%` в C++-имя `int32_t x = 5;`.
TEST_F(TranspilerTest, GenerateNativeVarManglesPercent) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "%x:Int32 := 5;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 2));

    auto nameTerm = Term::Create(TermID::NAME, "%x", range, parser::token_type::NAME);
    auto var =
        std::make_shared<VarDecl>(std::move(nameTerm), std::make_shared<IdentType>("Int32"), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "5"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticPassRunner runner(m_ctx);
        ASSERT_TRUE(runner.run(seq));
    }

    MapperFile out_idx = m_ctx.source().add_output("test_nativevar.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("int32_t x = 5;") != std::string::npos) << "result: " << result;
    EXPECT_EQ(result.find("%x"), std::string::npos) << "нативное '%' не должно попадать в C++: " << result;

    auto* reader = m_ctx.source().toReader();
    ASSERT_NE(reader, nullptr);
}

/// Test: throw statement (-- expr -- → throw expr;)
TEST_F(TranspilerTest, GenerateThrowStmt) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "-- value --;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 14));

    auto opTerm = Term::Create(TermID::INT_MINUS, "--", range, parser::token_type::END);
    auto stmt = std::make_shared<JumpStmt>(ParserToken::Kind::ThrowStmt, std::move(opTerm));
    stmt->m_value = std::make_shared<IdentName>("value");
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(stmt));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_throw.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("throw c_value;") != std::string::npos) << "result: " << result;
}

/// Test: throw void (-- -- → throw;)
TEST_F(TranspilerTest, GenerateThrowVoid) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "-- --;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 7));

    auto opTerm = Term::Create(TermID::INT_MINUS, "--", range, parser::token_type::END);
    auto stmt = std::make_shared<JumpStmt>(ParserToken::Kind::ThrowStmt, std::move(opTerm));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(stmt));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
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
    seq.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::move(litTerm)));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_literal.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("42;") != std::string::npos) << "result: " << result;
}

/// Test: standalone narrow string literal ('hello' → "hello";)
TEST_F(TranspilerTest, GenerateStandaloneStringLiteral) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "'hello';", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 9));

    auto litTerm = Term::Create(TermID::STRCHAR, "hello", range, parser::token_type::END);
    std::vector<AstNodePtr> seq;
    seq.push_back(std::make_shared<Literal>(ParserToken::Kind::StrChar, std::move(litTerm)));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
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
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42; %foo():Void := { };", true);
    MapperRange var_range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 9));
    MapperRange func_range(m_ctx.source().makeLoc(input_file, 10), m_ctx.source().makeLoc(input_file, 28));

    std::string varName = "x";
    auto varTerm = Term::Create(TermID::NAME, varName, var_range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(varTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    auto funcTerm = Term::Create(TermID::NAME, "%foo", func_range, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_type = std::make_shared<IdentType>("None");
    func->m_body = std::vector<AstNodePtr>{};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));
    seq.push_back(std::move(func));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_exports.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    const auto& exports = gen.exports();
    ASSERT_EQ(exports.size(), 2u);
    EXPECT_EQ(exports[0].trustName, "x");
    EXPECT_EQ(exports[0].cppName, "c_x");
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
    auto seq_node = std::make_shared<Sequence>(ParserToken::Kind::sequence, std::move(seqTerm));

    std::string xName = "x";
    auto xTerm = Term::Create(TermID::NAME, xName, range_x, parser::token_type::NAME);
    seq_node->m_body.push_back(std::make_shared<VarDecl>(std::move(xTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1")));

    std::string yName = "y";
    auto yTerm = Term::Create(TermID::NAME, yName, range_y, parser::token_type::NAME);
    seq_node->m_body.push_back(std::make_shared<VarDecl>(std::move(yTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2")));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(seq_node));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_seq.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("bool c_x = 1;") != std::string::npos) << "result: " << result;
    EXPECT_TRUE(result.find("int8_t c_y = 2;") != std::string::npos) << "result: " << result;
}

/// Test: narrow string literal with embedded quote is escaped ('a"b' → "a\"b")
TEST_F(TranspilerTest, GenerateStringWithEscapedQuote) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "s := 'a\"b';", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 7));

    std::string name = "s";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::StrChar, "a\\\"b"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_esc.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("std::string c_s = \"a\\\"b\";") != std::string::npos) << "result: " << result;
}

/// Test: атрибут иммутабельности (attr::ReadOnly) не меняет генерируемый код (current behaviour).
TEST_F(TranspilerTest, GenerateReadOnlyAttrUnchanged) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 13));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    var->add_attr(m_ctx.attrs().lookup(attr::ReadOnly).value()); // иммутабельная переменная (атрибут)
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    {
        SemanticPassRunner runner(m_ctx);
        runner.run(seq);
    }

    MapperFile out_idx = m_ctx.source().add_output("test_mut.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());

    CppTranspiler gen(m_ctx);
    gen.generateToFile(seq, out_idx);

    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_TRUE(result.find("const int8_t c_x = 42;") != std::string::npos) << "result: " << result;
}

/// Test: `x := :Int32` невалидно - в `:=` справа должно быть значение, а не тип-имя
/// (тип объявляется через `::=`). Семантика выдаёт явную ошибку.
TEST_F(TranspilerTest, GenerateTypeNameExprIsError) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := :Int32;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 13));

    std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<IdentType>("Int32"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

/// Test: нетипизированная переменная с инициализатором, у которой тип не выведен (семантика не
/// запускалась → inferredType INVALID) и таблица символов недоступна → ЯВНАЯ диагностика, а НЕ
/// тихий fallback на std::any (AGENTS rule 5 / без fallback).
TEST_F(TranspilerTest, GenerateUntypedVarUnknownTypeIsErrorNoFallback) {
    MapperFile input_file = m_ctx.source().add_source("test.src", "x := 42;", true);
    MapperRange range(m_ctx.source().makeLoc(input_file, 1), m_ctx.source().makeLoc(input_file, 8));

    auto term = Term::Create(TermID::NAME, "x", range, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));
    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    MapperFile out_idx = m_ctx.source().add_output("test_err.cpp", true);
    ASSERT_FALSE(out_idx.isInvalid());
    CppTranspiler gen(m_ctx); // без разрешённой таблицы символов
    gen.generateToFile(seq, out_idx);

    // Диагностика ошибки вывода (нет тихого std::any).
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
    std::string result = m_ctx.source().output_result(out_idx);
    EXPECT_EQ(result.find("std::any"), std::string::npos) << "no silent std::any fallback: " << result;
}

/// Test: addNameMapping for variable name is queryable via getCppName.

} // namespace trust
