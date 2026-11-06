// test/unit/semantic/arg_casts_test.cpp
// Материализация целей неявных конверсий: семантика записывает argCasts по выбранной
// сигнатуре, кодоген только печатает (CallExpr - аргументы, Binary - операнды оператора).
#include "semantic/typer/arg_casts.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/binary_op.hpp"
#include "diag/diag.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace trust {

namespace {

class ArgCastsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

} // namespace

TEST_F(ArgCastsTest, CallCastsFollowResolvedSignatureAndSkipAny) {
    TypeSystem& reg = *m_types;
    const TypeId ret = reg.read().getType(type::Bool);
    const TypeId any = reg.read().getType(type_generic::Any);
    const TypeId sig = reg.interner().getOrCreateFunctionType(ret, {reg.read().getType(type::Int32), any});

    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::make_shared<IdentName>("f"));
    call->resolvedSignature = sig;
    call->m_args = std::vector<AstNodePtr>{std::make_shared<IdentName>("a"), std::make_shared<IdentName>("b")};

    std::vector<AstNodePtr> roots{call};
    materializeArgCasts(roots, m_ctx);

    ASSERT_EQ(call->argCasts.size(), 2u);
    EXPECT_EQ(call->argCasts[0], reg.read().getType(type::Int32));
    EXPECT_EQ(call->argCasts[1], INVALID_TYPE_ID); // Any приводить некуда
}

TEST_F(ArgCastsTest, FreeOperatorMaterializesBothOperands) {
    TypeSystem& reg = *m_types;
    const TypeId ret = reg.read().getType(type::Bool);
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId sig = reg.interner().getOrCreateFunctionType(ret, {i32, i64});

    auto bin = std::make_shared<Binary>(ParserToken::Kind::CompareOp, std::make_shared<IdentName>("a"), std::make_shared<IdentName>("b"));
    bin->resolvedOperatorSignature = sig;
    bin->resolvedOperatorIsMember = false;

    std::vector<AstNodePtr> roots{bin};
    materializeArgCasts(roots, m_ctx);

    ASSERT_EQ(bin->argCasts.size(), 2u);
    EXPECT_EQ(bin->argCasts[0], i32);
    EXPECT_EQ(bin->argCasts[1], i64);
}

TEST_F(ArgCastsTest, MemberOperatorMaterializesRightOperandOnly) {
    TypeSystem& reg = *m_types;
    const TypeId ret = reg.read().getType(type::Bool);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId sig = reg.interner().getOrCreateFunctionType(ret, {i64});

    auto bin = std::make_shared<Binary>(ParserToken::Kind::CompareOp, std::make_shared<IdentName>("a"), std::make_shared<IdentName>("b"));
    bin->resolvedOperatorSignature = sig;
    bin->resolvedOperatorIsMember = true;

    std::vector<AstNodePtr> roots{bin};
    materializeArgCasts(roots, m_ctx);

    ASSERT_EQ(bin->argCasts.size(), 1u);
    EXPECT_EQ(bin->argCasts[0], i64);
}

TEST_F(ArgCastsTest, MemberIncDecIsNotMaterialized) {
    // Member `operator++()` не имеет явных параметров и эмитится emitIncDec, а не
    // emitOperatorDispatch: materializer не должен требовать форму binary-оператора.
    TypeSystem& reg = *m_types;
    const TypeId ret = reg.read().getType(type::Int32);
    const TypeId sig = reg.interner().getOrCreateFunctionType(ret, {});

    auto bin = std::make_shared<Binary>(ParserToken::Kind::IncDecOp, std::make_shared<IdentName>("x"), nullptr);
    bin->m_op = BinaryOp::PreIncrement;
    bin->resolvedOperatorSignature = sig;
    bin->resolvedOperatorIsMember = true;

    std::vector<AstNodePtr> roots{bin};
    materializeArgCasts(roots, m_ctx);

    EXPECT_TRUE(bin->argCasts.empty());
}

} // namespace trust
