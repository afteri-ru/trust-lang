// test/unit/semantic/verify_typed_ast_test.cpp
// Гейт типизированной AST: проверка материализации фактов, которые кодоген читает без
// пере-резолва. Нарушение — ICE-политика (здесь проверяется возвращаемое описание).
#include "semantic/verify/verify_typed_ast.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/binary_op.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace trust {

namespace {

class VerifyTypedAstTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    [[nodiscard]] std::optional<std::string> verify(const std::vector<AstNodePtr>& roots) const { return verifyTypedAst(*m_types, roots); }

    [[nodiscard]] QualType q(TypeId id) const { return QualType::fromTypeId(id); }

    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

AstNodePtr intLit() {
    return std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");
}

} // namespace

TEST_F(VerifyTypedAstTest, ValidMinimalTreePasses) {
    const TypeId i32 = m_types->read().getType(type::Int32);
    const TypeId b = m_types->read().getType(type::Bool);
    const TypeId sig = m_types->interner().getOrCreateFunctionType(i32, {i32});

    auto lit = intLit();
    static_cast<Literal&>(*lit).typeState = q(i32);

    auto var = std::make_shared<VarDecl>("x", nullptr, lit);
    var->inferredType = q(i32);

    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::make_shared<IdentName>("x"), std::make_shared<IdentName>("y"));
    add->m_op = BinaryOp::Add;
    add->resultType = q(i32);
    add->m_overflowCheck = true;

    auto cmp = std::make_shared<Binary>(ParserToken::Kind::CompareOp, std::make_shared<IdentName>("x"), std::make_shared<IdentName>("y"));
    cmp->m_op = BinaryOp::TypeIsA;
    cmp->resultType = q(b);
    cmp->m_typeCheckConst = true;

    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::make_shared<IdentName>("f"));
    call->m_args = std::vector<AstNodePtr>{std::make_shared<IdentName>("x")};
    call->resolvedSignature = sig;
    call->argCasts = std::vector<TypeId>{i32};
    call->resolvedTemplateArgs = std::vector<TypeId>{i32};

    auto ref = std::make_shared<RefMakeExpr>(ParserToken::Kind::RefMakeExpr);
    ref->m_resultType = q(i32);

    auto destr = std::make_shared<DestructureDecl>();
    destr->m_source = std::make_shared<IdentName>("d");
    destr->m_sourceType = q(i32);

    EXPECT_EQ(verify({lit, var, add, cmp, call, ref, destr}), std::nullopt);
}

TEST_F(VerifyTypedAstTest, VarDeclWithoutInferredTypeFails) {
    auto var = std::make_shared<VarDecl>("x", nullptr, intLit()); // inferredType не задан
    const auto failure = verify({var});
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->find("VarDecl::inferredType"), std::string::npos);
}

TEST_F(VerifyTypedAstTest, TypedForwardVarDeclIsExempt) {
    auto typeNode = std::make_shared<IdentName>("Int32");
    auto var = std::make_shared<VarDecl>("x", typeNode, nullptr); // x:Int32 := ...;
    EXPECT_EQ(verify({var}), std::nullopt);
}

TEST_F(VerifyTypedAstTest, TypeCheckWithoutFoldFails) {
    auto cmp = std::make_shared<Binary>(ParserToken::Kind::CompareOp, std::make_shared<IdentName>("x"), std::make_shared<IdentName>("y"));
    cmp->m_op = BinaryOp::TypeIsA;
    cmp->resultType = q(m_types->read().getType(type::Bool));
    const auto failure = verify({cmp});
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->find("m_typeCheckConst"), std::string::npos);
}

TEST_F(VerifyTypedAstTest, OverflowWithoutClassificationFails) {
    auto add = std::make_shared<Binary>(ParserToken::Kind::MathOp, std::make_shared<IdentName>("x"), std::make_shared<IdentName>("y"));
    add->m_op = BinaryOp::Add;
    add->resultType = q(m_types->read().getType(type::Int32));
    const auto failure = verify({add});
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->find("m_overflowCheck"), std::string::npos);
}

TEST_F(VerifyTypedAstTest, CallArgCastsSizeMismatchFails) {
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::make_shared<IdentName>("f"));
    call->m_args = std::vector<AstNodePtr>{std::make_shared<IdentName>("a"), std::make_shared<IdentName>("b")};
    call->argCasts = std::vector<TypeId>{m_types->read().getType(type::Int32)};
    const auto failure = verify({call});
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->find("argCasts"), std::string::npos);
}

TEST_F(VerifyTypedAstTest, CallSignatureMustBeFunctionType) {
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::make_shared<IdentName>("f"));
    call->resolvedSignature = m_types->read().getType(type::Int32); // не функциональный тип
    const auto failure = verify({call});
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->find("resolvedSignature"), std::string::npos);
}

} // namespace trust
