// test/unit/semantic/multilevel_var_test.cpp
// Юнит-тесты для проверки создания переменных на нескольких уровнях вложенности
// и регистрации/использования нового типа-синонима (alias на Int32).
//
// Узлы AST создаются программно (manual-конструкторы без TermPtr), поэтому
// range()/text() из исходника недоступны, но для семантики это не требуется.

#include "support/cpp_facts_test_util.hpp"
#include "semantic/core/pass_runner.hpp"
#include "symbols/symbol_table.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/ident_name.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/diag.hpp"
#include "types/type_system.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "utils/utils.hpp"
#include "gtest/gtest.h"

#include <sstream>
#include <string>
#include <vector>

namespace trust {
namespace {

class VarTypeFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        test::installTargetFacts(m_ctx, m_target);
    }

    void TearDown() override { setErrs(m_prev_err); }

    std::ostream* m_prev_err = nullptr;
    std::ostringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    TargetContract m_target;

    /// x:Type := lit  - типизированное объявление переменной.
    AstNodePtr typedVar(const std::string& name, const std::string& type, const std::string& lit) {
        return std::make_shared<VarDecl>(name, std::make_shared<IdentType>(type), std::make_shared<Literal>(ParserToken::Kind::IntLiteral, lit));
    }

    /// x := ident  - объявление с инициализатором-ссылкой на другую переменную.
    AstNodePtr refVar(const std::string& name, const std::string& ref) { return std::make_shared<VarDecl>(name, nullptr, std::make_shared<IdentName>(ref)); }

    /// myType ::= base  - алиас типа (TypeDecl).
    AstNodePtr typeAlias(const std::string& name, const std::string& base) {
        return std::make_shared<Binary>(ParserToken::Kind::TypeDecl, std::make_shared<IdentName>(name), std::make_shared<IdentType>(base));
    }

    TypeId int32Id() { return m_ctx.readTypes().getType("Int32"); }
};

// -- Переменные на нескольких уровнях вложенности ---------------

TEST_F(VarTypeFixture, GlobalAndNestedBlocks) {
    // g:Int32 := 1;  { a:Int32 := 2; { b:Int32 := 3; } }
    auto inner = std::make_shared<ScopeBlock>(std::string(""));
    inner->m_body.push_back(typedVar("b", "Int32", "3"));

    auto outer = std::make_shared<ScopeBlock>(std::string(""));
    outer->m_body.push_back(typedVar("a", "Int32", "2"));
    outer->m_body.push_back(std::move(inner));

    std::vector<AstNodePtr> seq;
    seq.push_back(typedVar("g", "Int32", "1"));
    seq.push_back(std::move(outer));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    // Глобальная переменная видна на глобальном уровне.
    const Symbol* g = runner.analysis().symbols().global().lookup(exportedKeyOf("g"));
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->type.getTypeId(), int32Id());
    EXPECT_NE(g->type.getTypeId(), INVALID_TYPE_ID);

    // Вложенные переменные были объявлены во вложенных скоупах и корректно
    // удалены после выхода из них (run() == true без ошибок - типы резолвятся).
    EXPECT_EQ(runner.analysis().symbols().resolve("a"), nullptr);
    EXPECT_EQ(runner.analysis().symbols().resolve("b"), nullptr);
}

TEST_F(VarTypeFixture, NestedScopeShadowing) {
    // x:Int32 := 1;  { x:Int64 := 2;  { x:Int32 := 3; } }
    auto inner = std::make_shared<ScopeBlock>(std::string(""));
    inner->m_body.push_back(typedVar("x", "Int32", "3"));

    auto outer = std::make_shared<ScopeBlock>(std::string(""));
    outer->m_body.push_back(typedVar("x", "Int64", "2"));
    outer->m_body.push_back(std::move(inner));

    std::vector<AstNodePtr> seq;
    seq.push_back(typedVar("x", "Int32", "1"));
    seq.push_back(std::move(outer));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq)); // shadowing на разных уровнях - не ошибка
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const Symbol* g = runner.analysis().symbols().global().lookup(exportedKeyOf("x"));
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->type.getTypeId(), int32Id());
}

TEST_F(VarTypeFixture, DuplicateInSameNestedScope) {
    // { x:Int32 := 1; x:Int64 := 2; } - дубликат в одном скоупе → ошибка.
    auto block = std::make_shared<ScopeBlock>(std::string(""));
    block->m_body.push_back(typedVar("x", "Int32", "1"));
    block->m_body.push_back(typedVar("x", "Int64", "2"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(block));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

TEST_F(VarTypeFixture, UndefinedNameInNestedScope) {
    // { y := z; } - z не объявлена внутри блока → ошибка undefined name.
    auto block = std::make_shared<ScopeBlock>(std::string(""));
    block->m_body.push_back(refVar("y", "z"));

    std::vector<AstNodePtr> seq;
    seq.push_back(std::move(block));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_FALSE(runner.run(seq));
    EXPECT_GT(m_ctx.diag().errorCount(), 0);
}

// -- Регистрация нового типа-синонима (Int32) -------------------

TEST_F(VarTypeFixture, TypeAliasIntSynonym) {
    // MyInt ::= Int32;
    std::vector<AstNodePtr> seq;
    seq.push_back(typeAlias("MyInt", "Int32"));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    // Алиас зарегистрирован в скоупе как узел TypeDecl.
    const Symbol* sym = runner.analysis().symbols().resolve("MyInt");
    ASSERT_NE(sym, nullptr);
    ASSERT_NE(sym->decl, nullptr);
    EXPECT_EQ(sym->decl->kind(), ParserToken::Kind::TypeDecl);
    TypeId alias = sym->type.getTypeId();
    EXPECT_NE(alias, INVALID_TYPE_ID);
    EXPECT_NE(alias, int32Id()); // алиас - отдельный TypeId

    // Проверка через реестр типов: канонический тип и C++-имя.
    // У алиаса нет собственного C++-имени - оно берётся у канонического типа.
    EXPECT_EQ(m_ctx.readTypes().store().desugaredTypeId(alias), int32Id());
    EXPECT_TRUE(m_ctx.readTypes().findType("MyInt").has_value());
    EXPECT_EQ(CppTypeRenderer(m_ctx.readTypes(), test::cppFactsViewOf(m_ctx)).render(m_ctx.readTypes().store().desugaredTypeId(alias)).value_or(""), "int32_t");
}

TEST_F(VarTypeFixture, VarWithAliasType) {
    // MyInt ::= Int32;  x:MyInt := 5;
    std::vector<AstNodePtr> seq;
    seq.push_back(typeAlias("MyInt", "Int32"));
    seq.push_back(typedVar("x", "MyInt", "5"));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const Symbol* x = runner.analysis().symbols().resolve("x");
    ASSERT_NE(x, nullptr);
    // Переменная получила TypeId алиаса, а не базового типа.
    const Symbol* alias = runner.analysis().symbols().resolve("MyInt");
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(x->type.getTypeId(), alias->type.getTypeId());
    EXPECT_EQ(m_ctx.readTypes().store().desugaredTypeId(x->type.getTypeId()), int32Id());
}

TEST_F(VarTypeFixture, AliasChain) {
    // MyInt ::= Int32;  Big ::= MyInt;
    std::vector<AstNodePtr> seq;
    seq.push_back(typeAlias("MyInt", "Int32"));
    seq.push_back(typeAlias("Big", "MyInt"));

    SemanticPassRunner runner(m_ctx, *m_types);
    EXPECT_TRUE(runner.run(seq));
    EXPECT_EQ(m_ctx.diag().errorCount(), 0);

    const Symbol* big = runner.analysis().symbols().resolve("Big");
    ASSERT_NE(big, nullptr);
    // Цепочка алиасов разворачивается до базового Int32.
    EXPECT_EQ(m_ctx.readTypes().store().desugaredTypeId(big->type.getTypeId()), int32Id());
    EXPECT_EQ(CppTypeRenderer(m_ctx.readTypes(), test::cppFactsViewOf(m_ctx)).render(m_ctx.readTypes().store().desugaredTypeId(big->type.getTypeId())).value_or(""), "int32_t");
}

} // namespace
} // namespace trust
