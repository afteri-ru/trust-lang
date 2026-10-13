// test/unit/ir/mir_build_test.cpp
// buildMir: все функции/лямбды собираются один раз в порядке DFS (внешняя → вложенные),
// модуль сохраняет корни HIR для анализов, читающих AST-факты.
#include "ir/cfg.hpp"
#include "ir/mir/mir.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_pool.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace trust::flow {
namespace {

std::shared_ptr<FuncDecl> func(std::string name, std::vector<AstNodePtr> body = {}) {
    auto node = std::make_shared<FuncDecl>(std::move(name));
    node->m_body = std::move(body);
    return node;
}

} // namespace

TEST(MirBuild, CollectsFunctionsInDfsOrderAndKeepsRoots) {
    AttrPool attrs;
    auto nested = func("inner");
    auto outer = func("outer", {nested});

    std::vector<AstNodePtr> roots{outer};
    MirModule mir = buildMir(roots, attrs);

    ASSERT_EQ(mir.functions.size(), 2u);
    EXPECT_EQ(mir.functions[0].func, outer.get());
    EXPECT_EQ(mir.functions[1].func, nested.get());
    EXPECT_EQ(mir.roots.size(), roots.size());
    EXPECT_EQ(mir.roots[0], roots[0]);
}

TEST(MirBuild, SkipsForwardDeclarations) {
    AttrPool attrs;
    auto forward = std::make_shared<FuncDecl>("forward"); // m_body = nullopt
    auto defined = func("defined");

    std::vector<AstNodePtr> roots{forward, defined};
    MirModule mir = buildMir(roots, attrs);

    ASSERT_EQ(mir.functions.size(), 1u);
    EXPECT_EQ(mir.functions[0].func, defined.get());
}

TEST(MirOpName, AllOpsRoundTrip) {
    EXPECT_EQ(mirOpName(MirOp::Declare), std::string_view("declare"));
    EXPECT_EQ(mirOpName(MirOp::Read), std::string_view("read"));
    EXPECT_EQ(mirOpName(MirOp::Write), std::string_view("write"));
    EXPECT_EQ(mirOpName(MirOp::Move), std::string_view("move"));
    EXPECT_EQ(mirOpName(MirOp::Call), std::string_view("call"));
    EXPECT_EQ(mirOpName(MirOp::ScopeExit), std::string_view("scope-exit"));
    EXPECT_EQ(mirOpName(MirOp::Assert), std::string_view("assert"));
    EXPECT_EQ(mirOpName(MirOp::Invariant), std::string_view("invariant"));
    EXPECT_EQ(mirOpName(MirOp::Terminator), std::string_view("terminator"));
}

TEST(MirOpName, InvalidValueFaults) {
    EXPECT_THROW((void)mirOpName(static_cast<MirOp>(255)), std::runtime_error);
}

} // namespace trust::flow
