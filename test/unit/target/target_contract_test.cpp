#include "session/context.hpp"
#include "target/target.hpp"

#include <gtest/gtest.h>

namespace trust {
namespace {

/// Новый контракт цели: `DeclCppFacts` живёт в `TargetContract` (вне языковой модели), не в `types`.
TEST(TargetContractTest, DeclFactsRoundTrip) {
    TargetContract target;
    DeclCppFacts& facts = target.declCppFacts();
    const DeclId id = DeclId::fromParts(7u, 3u);
    facts.setCppName(id, "std::sqrt");
    facts.setCppSuffix(id, "_P0i");
    EXPECT_EQ(facts.cppNameOf(id), "std::sqrt");
    EXPECT_EQ(facts.cppSuffixOf(id), "_P0i");
    // Имя и суффикс — разные факты одного id (не затирают друг друга); чужой id пуст.
    EXPECT_TRUE(facts.cppNameOf(DeclId::fromParts(7u, 4u)).empty());
    EXPECT_TRUE(facts.cppSuffixOf(DeclId::fromParts(7u, 4u)).empty());
}

/// Порт доступа: `Context::declCppFacts()` — та же зона, что `Context::target().declCppFacts()`.
TEST(TargetContractTest, ContextPortIsInjected) {
    Context ctx;
    TargetContract target;
    ctx.setTarget(&target);
    const DeclId id = DeclId::fromParts(1u, 1u);
    ctx.declCppFacts().setCppName(id, "ext");
    EXPECT_EQ(ctx.target().declCppFacts().cppNameOf(id), "ext");
    EXPECT_EQ(&ctx.declCppFacts(), &target.declCppFacts());
}

} // namespace
} // namespace trust
