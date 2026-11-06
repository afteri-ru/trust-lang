// test/unit/semantic/delegation_check_test.cpp
// Z6a: цель делегирования `:= name ...` не может совпадать с именем самого объявления.
#include "ast/ast_nodes.hpp"
#include "semantic/decl/func_signature_check.hpp"
#include "session/context.hpp"

#include <gtest/gtest.h>
#include <string>

namespace trust {
namespace {

TEST(DelegationCheckTest, SelfTargetIsRejected) {
    Context ctx;
    FuncDecl fn(std::string("c_str"));
    fn.m_definition = Definition::forwardTo("c_str");
    EXPECT_FALSE(semantic::validateDelegationTarget(ctx, fn));
}

TEST(DelegationCheckTest, DifferentTargetIsAccepted) {
    Context ctx;
    FuncDecl fn(std::string("length"));
    fn.m_definition = Definition::forwardTo("size");
    EXPECT_TRUE(semantic::validateDelegationTarget(ctx, fn));
}

TEST(DelegationCheckTest, NonDelegateIsAccepted) {
    Context ctx;
    FuncDecl fn(std::string("f"));
    fn.m_definition = Definition::fromBody({});
    EXPECT_TRUE(semantic::validateDelegationTarget(ctx, fn));
}

} // namespace
} // namespace trust