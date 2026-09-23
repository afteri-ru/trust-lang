// test/unit/ast/type_access_test.cpp - unit-тесты типизированного доступа к AST без RTTI
// (AstNodeBase::is<T>()/as<T>()) и типизированного доступа к trust-контрактам
// (trustContracts()/hasTrustProperty()) - R1.

#include "ast/ast_nodes.hpp"
#include "ast/trust_prop.hpp"
#include "gtest/gtest.h"
#include <memory>
#include <stdexcept>

namespace trust {
namespace {

TEST(AstTypeAccessTest, IsMatchesKindClass) {
    auto lit = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("42"));
    EXPECT_TRUE(lit->is<Literal>());
    EXPECT_FALSE(lit->is<Binary>());
    EXPECT_FALSE(lit->is<IdentName>());
}

TEST(AstTypeAccessTest, IsMatchesBaseClass) {
    // IdentType - производный от IdentName: is<IdentName>() должен быть true (проверка базового класса).
    auto ty = std::make_shared<IdentType>("Int32");
    EXPECT_TRUE(ty->is<IdentType>());
    EXPECT_TRUE(ty->is<IdentName>());
    // Обратное направление неверно: IdentName не является IdentType.
    auto id = std::make_shared<IdentName>("x");
    EXPECT_TRUE(id->is<IdentName>());
    EXPECT_FALSE(id->is<IdentType>());
}

TEST(AstTypeAccessTest, MultipleKindsOneClass) {
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp);
    auto assign = std::make_shared<Binary>(ParserToken::Kind::AssignOp);
    EXPECT_TRUE(add->is<Binary>());
    EXPECT_TRUE(assign->is<Binary>());
}

TEST(AstTypeAccessTest, AsReturnsCorrectPointer) {
    auto lit = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("42"));
    Literal* p = lit->as<Literal>();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p, lit.get());
    EXPECT_EQ(p->text(), "42");
}

TEST(AstTypeAccessTest, AsWrongKindFaults) {
    auto lit = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("42"));
    EXPECT_THROW((void)lit->as<Binary>(), std::runtime_error);
    auto id = std::make_shared<IdentName>("x");
    EXPECT_THROW((void)id->as<IdentType>(), std::runtime_error);
}

TEST(AstTypeAccessTest, ConstAsReturnsConstPointer) {
    const auto lit = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("7"));
    const Literal* p = lit->as<Literal>();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->text(), "7");
}

TEST(AstTypeAccessTest, TrustContractsReturnsContracts) {
    auto expr = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("1"));
    auto contract = std::make_shared<TrustContract>(ParserToken::Kind::TrustContract, expr, PropertyKind::Post);

    auto fn = std::make_shared<FuncDecl>(std::string("f"));
    fn->m_trust.push_back(contract);

    const auto contracts = fn->trustContracts();
    ASSERT_EQ(contracts.size(), 1u);
    EXPECT_EQ(contracts[0], contract.get());
    EXPECT_TRUE(fn->hasTrustProperty(PropertyKind::Post));
    EXPECT_FALSE(fn->hasTrustProperty(PropertyKind::Pre));
}

TEST(AstTypeAccessTest, TrustContractsRejectsNonContract) {
    // m_trust обязан содержать только TrustContract; инородный узел - ошибка логики (FAULT),
    // а НЕ тихое отбрасывание.
    auto foreign = std::make_shared<Literal>(ParserToken::Kind::IntLiteral, std::string("2"));
    auto fn = std::make_shared<FuncDecl>(std::string("f"));
    fn->m_trust.push_back(foreign);
    EXPECT_THROW((void)fn->trustContracts(), std::runtime_error);
    EXPECT_THROW((void)fn->hasTrustProperty(PropertyKind::Pre), std::runtime_error);
}

} // namespace
} // namespace trust
