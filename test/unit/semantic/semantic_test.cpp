#include "utils/io.hpp"
#include "semantic/analyzer.hpp"
#include "semantic/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "diag/context.hpp"
#include "diag/diag.hpp"
#include "syntax/term.h"
#include "syntax/term_types.h"
#include "gtest/gtest.h"
#include <sstream>
#include <string>
#include <vector>

namespace trust {
namespace {

class ErrsFixture : public ::testing::Test {
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

// ── Variable tests ───────────────────────────────────────

class SemanticTest : public ErrsFixture {};

TEST_F(SemanticTest, VarDeclSimple) {
    // x := 42
    const std::string name = "x";
    auto term = Term::Create(TermID::NAME, name, {}, parser::token_type::NAME);
    auto var = std::make_shared<VarDecl>(name, std::move(term), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"), false);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(analyzer.symbols().size(), 1);
    auto* sym = analyzer.symbols().lookup("x");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "x");
    EXPECT_TRUE(std::holds_alternative<VariableSymbolData>(sym->data));
    auto& var_data = std::get<VariableSymbolData>(sym->data);
    EXPECT_NE(var_data.init, nullptr);
    EXPECT_FALSE(var_data.is_mutable);
}

TEST_F(SemanticTest, DupDeclError) {
    // x := 1; x := 2;
    const std::string name1 = "x";
    auto t1 = Term::Create(TermID::NAME, name1, {}, parser::token_type::NAME);
    auto a1 = std::make_shared<VarDecl>(name1, std::move(t1), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"), false);

    const std::string name2 = "x";
    auto t2 = Term::Create(TermID::NAME, name2, {}, parser::token_type::NAME);
    auto a2 = std::make_shared<VarDecl>(name2, std::move(t2), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "2"), false);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(a1));
    seq.push_back(std::move(a2));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, UndefinedNameRef) {
    // y := z; — error: undefined name
    const std::string name = "y";
    auto term = Term::Create(TermID::NAME, name, {}, parser::token_type::NAME);

    const std::string refName = "z";
    auto refTerm = Term::Create(TermID::NAME, refName, {}, parser::token_type::NAME);
    auto ref = std::make_shared<IdentName>(refName, std::move(refTerm));

    auto var = std::make_shared<VarDecl>(name, std::move(term), nullptr, std::move(ref), false);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(var));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(SemanticTest, UseAfterDecl) {
    // a := 1; b := a;
    const std::string nameA = "a";
    auto tA = Term::Create(TermID::NAME, nameA, {}, parser::token_type::NAME);
    auto a1 = std::make_shared<VarDecl>(nameA, std::move(tA), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1"), false);

    const std::string nameB = "b";
    auto tB = Term::Create(TermID::NAME, nameB, {}, parser::token_type::NAME);
    auto a2 = std::make_shared<VarDecl>(nameB, std::move(tB), nullptr, std::make_shared<IdentName>("a"), false);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(a1));
    seq.push_back(std::move(a2));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(analyzer.symbols().size(), 2);
}

TEST_F(SemanticTest, LiteralStandalone) {
    auto litTerm = Term::Create(TermID::INTEGER, "42", {}, parser::token_type::END);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42", std::move(litTerm)));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_TRUE(ok);
}

// ── SymbolTable tests ────────────────────────────────────
// SymbolTable больше не живёт в Context — создаётся отдельно, принимает DiagnosticEngine&

class SymbolTableTest : public ErrsFixture {};

TEST_F(SymbolTableTest, AddLookup) {
    DiagnosticEngine diag;

    SymbolTable symtab(diag);
    Symbol sym;
    sym.name = "x";
    sym.sourceRange = MapperRange{};
    sym.data = VariableSymbolData{nullptr, false};

    EXPECT_TRUE(symtab.addSymbol(std::move(sym)));
    EXPECT_EQ(symtab.size(), 1);
    EXPECT_NE(symtab.lookup("x"), nullptr);
}

TEST_F(SymbolTableTest, DupRejected) {
    DiagnosticEngine diag;

    SymbolTable symtab(diag);
    Symbol s1, s2;
    s1.name = "x";
    s2.name = "x";
    s1.sourceRange = MapperRange{};
    s2.sourceRange = MapperRange{};
    s1.data = VariableSymbolData{nullptr, false};
    s2.data = VariableSymbolData{nullptr, false};

    EXPECT_TRUE(symtab.addSymbol(std::move(s1)));
    EXPECT_FALSE(symtab.addSymbol(std::move(s2)));
    EXPECT_GT(diag.errorCount(), 0);
}

TEST_F(SymbolTableTest, LookupNotFound) {
    DiagnosticEngine diag;
    SymbolTable symtab(diag);
    EXPECT_EQ(symtab.lookup("nonexistent"), nullptr);
}

// ── Function forward declaration tests ───────────────────

class FuncDeclTest : public ErrsFixture {};

TEST_F(FuncDeclTest, ForwardDeclNoReturn) {
    // func(arg:Int32) := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(funcName, std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};

    auto paramType = std::make_shared<IdentType>("Int32");
    func->m_params->push_back(std::make_shared<ParamDecl>("arg", paramType, nullptr));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(analyzer.symbols().size(), 1);

    auto* sym = analyzer.symbols().lookup("func");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->name, "func");
}

TEST_F(FuncDeclTest, ForwardDeclWithReturn) {
    // func(arg:Int32):Int32 := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(funcName, std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};

    auto paramType = std::make_shared<IdentType>("Int32");
    func->m_params->push_back(std::make_shared<ParamDecl>("arg", paramType, nullptr));
    func->m_type = std::make_shared<IdentType>("Int32");

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(analyzer.symbols().size(), 1);

    auto* sym = analyzer.symbols().lookup("func");
    ASSERT_NE(sym, nullptr);
}

TEST_F(FuncDeclTest, ForwardDeclMultipleParams) {
    // func(a:Int8, b:String) := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(funcName, std::move(funcTerm));
    func->m_params = std::vector<AstNodePtr>{};

    auto paType = std::make_shared<IdentType>("Int8");
    func->m_params->push_back(std::make_shared<ParamDecl>("a", paType, nullptr));
    auto pbType = std::make_shared<IdentType>("String");
    func->m_params->push_back(std::make_shared<ParamDecl>("b", pbType, nullptr));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(analyzer.symbols().size(), 1);

    auto* sym = analyzer.symbols().lookup("func");
    ASSERT_NE(sym, nullptr);
}

TEST_F(FuncDeclTest, ForwardDeclNoParams) {
    // func() := ... ;
    const std::string funcName = "func";
    auto funcTerm = Term::Create(TermID::NAME, funcName, {}, parser::token_type::NAME);
    auto func = std::make_shared<FuncDecl>(funcName, std::move(funcTerm));
    // no m_params / m_body = forward declaration

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(func));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_TRUE(ok);
    EXPECT_EQ(analyzer.symbols().size(), 1);

    auto* sym = analyzer.symbols().lookup("func");
    ASSERT_NE(sym, nullptr);
}

TEST_F(FuncDeclTest, DuplicateFuncName) {
    // func(x:Int32) := ... ; func(y:Int32) := ... ;
    const std::string f1Name = "func";
    auto f1Term = Term::Create(TermID::NAME, f1Name, {}, parser::token_type::NAME);
    auto f1 = std::make_shared<FuncDecl>(f1Name, std::move(f1Term));
    f1->m_params = std::vector<AstNodePtr>{};
    auto p1Type = std::make_shared<IdentType>("Int32");
    f1->m_params->push_back(std::make_shared<ParamDecl>("x", p1Type, nullptr));

    const std::string f2Name = "func";
    auto f2Term = Term::Create(TermID::NAME, f2Name, {}, parser::token_type::NAME);
    auto f2 = std::make_shared<FuncDecl>(f2Name, std::move(f2Term));
    f2->m_params = std::vector<AstNodePtr>{};
    auto p2Type = std::make_shared<IdentType>("Int32");
    f2->m_params->push_back(std::make_shared<ParamDecl>("y", p2Type, nullptr));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(f1));
    seq.push_back(std::move(f2));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(FuncDeclTest, FuncAndVarSameName) {
    // func(x:Int32) := ... ; func := 42; — error: duplicate
    const std::string fName = "func";
    auto fTerm = Term::Create(TermID::NAME, fName, {}, parser::token_type::NAME);
    auto f1 = std::make_shared<FuncDecl>(fName, std::move(fTerm));
    f1->m_params = std::vector<AstNodePtr>{};
    auto pType = std::make_shared<IdentType>("Int32");
    f1->m_params->push_back(std::make_shared<ParamDecl>("x", pType, nullptr));

    const std::string vName = "func";
    auto vTerm = Term::Create(TermID::NAME, vName, {}, parser::token_type::NAME);
    auto v1 = std::make_shared<VarDecl>(vName, std::move(vTerm), nullptr, std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "42"), false);

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(f1));
    seq.push_back(std::move(v1));

    SemanticAnalyzer analyzer(m_ctx);
    bool ok = analyzer.analyze(seq);

    EXPECT_FALSE(ok);
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

} // namespace
} // namespace trust