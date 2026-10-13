// test/unit/ast/clone_test.cpp
// Граница AST→HIR: origin клона (единственное хранимое направление соответствия), сохранение
// разделяемых поддеревьев и ремап ВСЕХ невладеющих ссылок (декларативный реестр в clone.cpp).
#include "ast/ast_nodes.hpp"
#include "ast/clone.hpp"
#include "ir/lowering.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace trust {
namespace {

using trust::ParserToken;

AstNodePtr intLit() {
    return std::make_shared<Literal>(ParserToken::Kind::IntLiteral, "1");
}

TEST(CloneTest, SetsOriginAndPreservesSharedSubtrees) {
    auto sharedLiteral = intLit();
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(sharedLiteral);
    body.push_back(sharedLiteral); // один и тот же объект дважды: клон — DAG, не копия
    fn->m_body = std::move(body);

    const std::vector<AstNodePtr> clones = cloneTrees({fn});
    ASSERT_EQ(clones.size(), 1u);
    const auto& fnClone = static_cast<const FuncDecl&>(*clones[0]);
    EXPECT_EQ(fnClone.origin, fn.get());
    ASSERT_TRUE(fnClone.m_body.has_value());
    ASSERT_EQ(fnClone.m_body->size(), 2u);
    const AstNodeBase* first = (*fnClone.m_body)[0].get();
    EXPECT_NE(first, sharedLiteral.get());
    EXPECT_EQ(first, (*fnClone.m_body)[1].get()) << "shared subtree must be cloned once";
    EXPECT_EQ(first->origin, sharedLiteral.get());
}

TEST(CloneTest, RemapsAllDeclaredNonOwningSlots) {
    auto decl = std::make_shared<VarDecl>("x", nullptr, intLit());
    auto use = std::make_shared<IdentName>("x");
    use->resolvedDecl = decl.get();
    auto arg = std::make_shared<ArgNode>("x");
    arg->resolvedDecl = decl.get();

    auto binary = std::make_shared<Binary>(ParserToken::Kind::MathOp, use, intLit());
    binary->m_typeDecl = decl.get();
    auto typedVar = std::make_shared<VarDecl>("y", std::make_shared<IdentType>("Int32"), nullptr);
    typedVar->m_typeDecl = decl.get();
    auto ret = std::make_shared<JumpStmt>(ParserToken::Kind::ReturnStmt);
    auto call = std::make_shared<CallExpr>(ParserToken::Kind::CallExpr, std::make_shared<IdentName>("g"));
    call->boundArgs = std::vector<const AstNodeBase*>{use.get()};

    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(decl);
    body.push_back(binary);
    body.push_back(typedVar);
    body.push_back(ret);
    body.push_back(call);
    body.push_back(arg);
    fn->m_body = std::move(body);
    static_cast<JumpStmt&>(*ret).m_funcDecl = fn.get();

    const std::vector<AstNodePtr> clones = cloneTrees({fn});
    ASSERT_EQ(clones.size(), 1u);
    const auto& fnClone = static_cast<const FuncDecl&>(*clones[0]);
    ASSERT_TRUE(fnClone.m_body.has_value());
    const std::vector<AstNodePtr>& out = *fnClone.m_body;
    ASSERT_EQ(out.size(), 6u);
    const AstNodeBase* declClone = out[0].get();
    EXPECT_NE(declClone, decl.get());

    const auto& binaryClone = static_cast<const Binary&>(*out[1]);
    ASSERT_NE(binaryClone.m_left, nullptr);
    const auto* identClone = binaryClone.m_left->as<IdentName>();
    ASSERT_NE(identClone, nullptr);
    EXPECT_EQ(identClone->resolvedDecl, declClone) << "IdentName::resolvedDecl";
    EXPECT_EQ(binaryClone.m_typeDecl, declClone) << "Binary::m_typeDecl";

    const auto& typedVarClone = static_cast<const VarDecl&>(*out[2]);
    EXPECT_EQ(typedVarClone.m_typeDecl, declClone) << "VarDecl::m_typeDecl";

    const auto& retClone = static_cast<const JumpStmt&>(*out[3]);
    EXPECT_EQ(retClone.m_funcDecl, &fnClone) << "JumpStmt::m_funcDecl";

    const auto& callClone = static_cast<const CallExpr&>(*out[4]);
    ASSERT_TRUE(callClone.boundArgs.has_value());
    ASSERT_EQ(callClone.boundArgs->size(), 1u);
    EXPECT_EQ((*callClone.boundArgs)[0], identClone) << "CallExpr::boundArgs";

    const auto& argClone = static_cast<const ArgNode&>(*out[5]);
    EXPECT_EQ(argClone.resolvedDecl, declClone) << "ArgNode::resolvedDecl";
}

TEST(CloneTest, LoweredWrapperHasNoOrigin) {
    // Lowering создаёт НОВЫЙ узел (SemicolonStmt) уже на клоне: источника в исходной AST у него нет.
    // Клон обёрнутого выражения сохраняет origin на исходный узел AST.
    auto expr = std::make_shared<Binary>(ParserToken::Kind::MathOp, intLit(), intLit());
    auto fn = std::make_shared<FuncDecl>("f");
    std::vector<AstNodePtr> body;
    body.push_back(expr);
    fn->m_body = std::move(body);

    std::vector<AstNodePtr> roots = cloneTrees({fn});
    LowerCtx ctx;
    lowerBody(roots, ctx);

    const auto& fnClone = static_cast<const FuncDecl&>(*roots[0]);
    ASSERT_TRUE(fnClone.m_body.has_value());
    ASSERT_EQ(fnClone.m_body->size(), 1u);
    const AstNodeBase* wrapper = (*fnClone.m_body)[0].get();
    ASSERT_EQ(wrapper->kind(), ParserToken::Kind::SemicolonStmt);
    EXPECT_EQ(wrapper->origin, nullptr) << "lowering-created node has no source AST node";
    const auto& semi = static_cast<const SemicolonStmt&>(*wrapper);
    ASSERT_NE(semi.m_expr, nullptr);
    EXPECT_EQ(semi.m_expr->origin, expr.get()) << "cloned AST node keeps its origin";
}

} // namespace
} // namespace trust
