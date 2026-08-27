#include "semantic/semantic_test_fixture.hpp"

namespace trust {
TEST_F(FuncDeclTest, ForwardDeclNoReturn) {
    // func(arg:Int32) := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};

    auto paramType = std::make_shared<IdentType>("Int32");
    func->m_params->push_back(std::make_shared<ArgNode>("arg", paramType));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "func");
}

TEST_F(FuncDeclTest, ForwardDeclWithReturn) {
    // func(arg:Int32):Int32 := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};

    auto paramType = std::make_shared<IdentType>("Int32");
    func->m_params->push_back(std::make_shared<ArgNode>("arg", paramType));
    func->m_type = std::make_shared<IdentType>("Int32");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
}

TEST_F(FuncDeclTest, ForwardDeclMultipleParams) {
    // func(a:Int8, b:String) := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};

    auto paType = std::make_shared<IdentType>("Int8");
    func->m_params->push_back(std::make_shared<ArgNode>("a", paType));
    auto pbType = std::make_shared<IdentType>("String");
    func->m_params->push_back(std::make_shared<ArgNode>("b", pbType));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
}

TEST_F(FuncDeclTest, ForwardDeclNoParams) {
    // func() := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    // no m_params / m_body = forward declaration

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(runner.analysis().symbols().globalSize(), 1);

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
}

TEST_F(FuncDeclTest, DuplicateFuncName) {
    // func(x:Int32) := ... ; func(y:Int32) := ... ;
    const std::string f1Name = "func";
    auto f1Term = Term::Create(TermID::NAME, f1Name, {}, parser::token_type::NAME);
    auto f1 = std::make_shared<FuncDecl>(std::move(f1Term));
    f1->m_params = std::vector<AstNodePtr>{};
    auto p1Type = std::make_shared<IdentType>("Int32");
    f1->m_params->push_back(std::make_shared<ArgNode>("x", p1Type));

    const std::string f2Name = "func";
    auto f2Term = Term::Create(TermID::NAME, f2Name, {}, parser::token_type::NAME);
    auto f2 = std::make_shared<FuncDecl>(std::move(f2Term));
    f2->m_params = std::vector<AstNodePtr>{};
    auto p2Type = std::make_shared<IdentType>("Int32");
    f2->m_params->push_back(std::make_shared<ArgNode>("y", p2Type));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(f1));
    seq.push_back(std::move(f2));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(FuncDeclTest, FuncAndVarSameName) {
    // func(x:Int32) := ... ; func := 42; - error: duplicate
    const std::string fName = "func";
    auto fTerm = Term::Create(TermID::NAME, fName, {}, parser::token_type::NAME);
    auto f1 = std::make_shared<FuncDecl>(std::move(fTerm));
    f1->m_params = std::vector<AstNodePtr>{};
    auto pType = std::make_shared<IdentType>("Int32");
    f1->m_params->push_back(std::make_shared<ArgNode>("x", pType));

    const std::string vName = "func";
    auto vTerm = Term::Create(TermID::NAME, vName, {}, parser::token_type::NAME);
    auto v1 = std::make_shared<VarDecl>(std::move(vTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(f1));
    seq.push_back(std::move(v1));

    SemanticPassRunner runner(m_ctx);
    bool ok = runner.run(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// -- Интеграция таблицы типов с анализом --

TEST_F(FuncDeclTest, BuildsFunctionType) {
    // func(arg:Int32):Bool := ... ; - строится FunctionTypeId сигнатуры.
    auto funcTerm = Term::Create(TermID::NAME, "func", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_params->push_back(std::make_shared<ArgNode>("arg", std::make_shared<IdentType>("Int32")));
    func->m_type = std::make_shared<IdentType>("Bool");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    ASSERT_TRUE(runner.run(seq));

    auto* sym = runner.analysis().symbols().resolve("func");
    ASSERT_NE(sym, nullptr);
    EXPECT_NE(sym->type, INVALID_TYPE_ID);

    const auto* ft = m_ctx.types().getTypeDataAs<FunctionTypeData>(sym->type);
    ASSERT_NE(ft, nullptr);
    auto int32 = m_ctx.types().findType("Int32");
    auto bool_id = m_ctx.types().findType("Bool");
    ASSERT_TRUE(int32.has_value());
    ASSERT_TRUE(bool_id.has_value());
    EXPECT_EQ(ft->returnType, *bool_id);
    ASSERT_EQ(ft->paramTypes.size(), 1u);
    EXPECT_EQ(ft->paramTypes[0], *int32);
}

// Forward-объявление функции `%f(a:Int32):Int32 := ...;` - нативная функция с типом возврата
// регистрируется без ошибки (FunctionTypeId сигнатуры).
TEST_F(FuncDeclTest, NativeForwardFuncWithReturnType) {
    auto funcTerm = Term::Create(TermID::NAME, "%f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};
    func->m_params->push_back(std::make_shared<ArgNode>("a", std::make_shared<IdentType>("Int32")));
    func->m_type = std::make_shared<IdentType>("Int32");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("%f");
    ASSERT_NE(sym, nullptr);
    EXPECT_NE(sym->type, INVALID_TYPE_ID);
}

// Forward-объявление нативной функции `%f() := ...;` без типа возврата → ошибка (нативные имена
// транслируются в C++ напрямую, поэтому тип обязателен).
TEST_F(FuncDeclTest, NativeForwardFuncNoReturnTypeError) {
    auto funcTerm = Term::Create(TermID::NAME, "%f", {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{}; // no return type (m_type = nullptr)

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticPassRunner runner(m_ctx);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// Forward-объявление функции завершается определением того же имени в том же скоупе
// (declareOrComplete → Completed): не ошибка, у символа появляется тело.
TEST_F(FuncDeclTest, ForwardFuncCompletedByDefinition) {
    auto mk = [] {
        auto t = Term::Create(TermID::NAME, "%f", {}, parser::token_type::NAME);
        auto f = std::make_shared<FuncDecl>(std::move(t));
        f->m_params = std::vector<AstNodePtr>{};
        f->m_params->push_back(std::make_shared<ArgNode>("a", std::make_shared<IdentType>("Int32")));
        f->m_type = std::make_shared<IdentType>("Int32");
        return f;
    };
    auto fwd = mk();
    auto def = mk();
    auto retTerm = Term::Create(TermID::NAME, "++", {}, parser::token_type::END);
    auto ret = std::make_shared<JumpStmt>(ParserToken::Kind::ReturnStmt, std::move(retTerm));
    ret->m_value = std::make_shared<IdentName>("a");
    def->m_body = std::vector<AstNodePtr>{std::move(ret)};

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(fwd));
    seq.push_back(std::move(def));

    SemanticPassRunner runner(m_ctx);
    EXPECT_TRUE(runner.run(seq));
    auto* sym = runner.analysis().symbols().resolve("%f");
    ASSERT_NE(sym, nullptr);
    ASSERT_NE(sym->decl, nullptr);
    EXPECT_EQ(sym->decl->kind(), ParserToken::Kind::FuncDecl);
    EXPECT_TRUE(static_cast<const FuncDecl&>(*sym->decl).m_body.has_value()) << "определение должно заменить forward-объявление";
}

// Forward-объявление переменной `x:Int32 := ...;` (без инициализатора) - регистрируется в

} // namespace trust
