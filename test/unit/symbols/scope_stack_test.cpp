#include "symbols/table/scope_stack.hpp"

#include "ast/ast_nodes.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <vector>

namespace trust {
namespace {

// -- ScopeStack: единственный контейнер объявлений и локальных фактов скоупа ---------------

TEST(ScopeStack, GlobalScopeAlwaysPresent) {
    ScopeStack stack;
    EXPECT_EQ(stack.depth(), 1u);
    EXPECT_EQ(stack.currentCreator(), nullptr);
    EXPECT_EQ(stack.current().creator, nullptr);
    EXPECT_EQ(stack.front().creator, nullptr);
}

TEST(ScopeStack, PushPopRestoresCreator) {
    ScopeStack stack;
    auto block = std::make_shared<ScopeBlock>("blk");
    stack.push(block.get());
    EXPECT_EQ(stack.depth(), 2u);
    EXPECT_EQ(stack.currentCreator(), block.get());

    stack.push();
    EXPECT_EQ(stack.depth(), 3u);
    EXPECT_EQ(stack.currentCreator(), nullptr);

    stack.pop();
    EXPECT_EQ(stack.currentCreator(), block.get());
    stack.pop();
    EXPECT_EQ(stack.depth(), 1u);
    // Глобальный скоуп не удаляется.
    stack.pop();
    EXPECT_EQ(stack.depth(), 1u);
}

TEST(ScopeStack, PopKeepsScopeAndSymbolsAlive) {
    ScopeStack stack;
    auto block = std::make_shared<ScopeBlock>("blk");
    stack.push(block.get());
    const std::size_t blockIndex = stack.currentIndex();
    const std::size_t globalIndex = 0;

    stack.current().symbols.emplace(exportedKeyOf("x"), Symbol{});
    const Symbol* stored = stack.current().lookup(exportedKeyOf("x"));
    ASSERT_NE(stored, nullptr);

    stack.pop();
    EXPECT_EQ(stack.depth(), 1u);
    EXPECT_TRUE(stack.node(blockIndex).closed) << "popped scope must be marked closed";
    // persistent: узел и его объявления живы, адрес символа не изменился.
    EXPECT_EQ(stack.size(), 2u);
    EXPECT_NE(stack.node(blockIndex).lookup(exportedKeyOf("x")), nullptr);
    EXPECT_EQ(stack.node(blockIndex).lookup(exportedKeyOf("x")), stored);
    EXPECT_EQ(stack.node(blockIndex).creator, block.get());
    EXPECT_EQ(stack.node(blockIndex).parent, globalIndex);
    EXPECT_EQ(stack.node(globalIndex).parent, Scope::kNoParent);
    // лексический поиск закрытое имя не видит
    EXPECT_EQ(stack.current().lookup(exportedKeyOf("x")), nullptr);
}

TEST(ScopeStack, ActivePathTracksNesting) {
    ScopeStack stack;
    auto outer = std::make_shared<ScopeBlock>("outer");
    stack.push(outer.get());
    const std::size_t outerIndex = stack.currentIndex();
    auto inner = std::make_shared<ScopeBlock>("inner");
    stack.push(inner.get());
    const std::size_t innerIndex = stack.currentIndex();

    ASSERT_EQ(stack.activePath().size(), 3u);
    EXPECT_EQ(stack.activePath().at(1), outerIndex);
    EXPECT_EQ(stack.activePath().at(2), innerIndex);
    EXPECT_EQ(stack.node(innerIndex).parent, outerIndex);
    EXPECT_EQ(stack.at(1).creator, outer.get());

    stack.pop();
    EXPECT_EQ(stack.activePath().size(), 2u);
    EXPECT_TRUE(stack.node(innerIndex).closed);
    EXPECT_FALSE(stack.node(outerIndex).closed);
}

TEST(ScopeStack, MoveKeepsSymbolAddresses) {
    ScopeStack stack;
    stack.push();
    stack.current().symbols.emplace(exportedKeyOf("x"), Symbol{});
    const Symbol* stored = stack.current().lookup(exportedKeyOf("x"));
    const std::size_t blockIndex = stack.currentIndex();

    ScopeStack moved(std::move(stack));
    ASSERT_EQ(moved.size(), 2u);
    EXPECT_EQ(moved.node(blockIndex).lookup(exportedKeyOf("x")), stored);
    EXPECT_EQ(moved.depth(), 2u);
}

TEST(ScopeStack, IterationGoesFromInnerToGlobal) {
    ScopeStack stack;
    auto outer = std::make_shared<ScopeBlock>("outer");
    auto inner = std::make_shared<ScopeBlock>("inner");
    stack.push(outer.get());
    stack.push(inner.get());

    std::vector<const Scope*> visited;
    stack.forEachFromInner([&](const Scope& scope) { visited.push_back(&scope); });

    ASSERT_EQ(visited.size(), 3u);
    EXPECT_EQ(visited[0]->creator, inner.get());
    EXPECT_EQ(visited[1]->creator, outer.get());
    EXPECT_EQ(visited[2]->creator, nullptr);
}

} // namespace
} // namespace trust
