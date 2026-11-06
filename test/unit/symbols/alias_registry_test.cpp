#include "symbols/import/alias_registry.hpp"

#include "gtest/gtest.h"

namespace trust {
namespace {

// -- AliasRegistry: единственное хранилище using-областей и namespace-алиасов ----------------

TEST(AliasRegistry, UsingIsRecordedOncePerScope) {
    AliasRegistry registry;
    Scope scope;
    registry.recordUsing(scope, UsingScopeRecord{ModuleIndex{1}, "std", MapperRange{}, MapperRange{}});
    registry.recordUsing(scope, UsingScopeRecord{ModuleIndex{1}, "std", MapperRange{}, MapperRange{}});
    ASSERT_EQ(scope.usingRefs.size(), 1u);
    EXPECT_EQ(registry.usingAt(scope.usingRefs.front()).path, "std");
    EXPECT_EQ(registry.usingProjection().size(), 1u);

    // Другой путь — отдельная запись.
    registry.recordUsing(scope, UsingScopeRecord{ModuleIndex{1}, "other", MapperRange{}, MapperRange{}});
    EXPECT_EQ(scope.usingRefs.size(), 2u);
    EXPECT_EQ(registry.usingProjection().size(), 2u);
}

TEST(AliasRegistry, AliasConflictsRejectedInSameScope) {
    AliasRegistry registry;
    Scope scope;
    EXPECT_TRUE(registry.recordAlias(scope, NamespaceAliasRecord{ModuleIndex{1}, "ns", ModuleIndex{2}, "inner", MapperRange{}, AliasOrigin::Local}));
    EXPECT_TRUE(registry.recordAlias(scope, NamespaceAliasRecord{ModuleIndex{1}, "ns", ModuleIndex{2}, "inner", MapperRange{}, AliasOrigin::Local}));
    ASSERT_EQ(scope.aliasRefs.size(), 1u);
    // Тот же алиас к другой цели в том же скоупе — конфликт.
    EXPECT_FALSE(registry.recordAlias(scope, NamespaceAliasRecord{ModuleIndex{1}, "ns", ModuleIndex{3}, "other", MapperRange{}, AliasOrigin::Local}));
    // Пустые аргументы — не привязка.
    EXPECT_FALSE(registry.recordAlias(scope, NamespaceAliasRecord{ModuleIndex{1}, "", ModuleIndex{2}, "inner", MapperRange{}, AliasOrigin::Local}));
}

TEST(AliasRegistry, PromotedAliasLivesInRootScope) {
    AliasRegistry registry;
    Scope local;
    Scope root;
    ASSERT_TRUE(registry.recordAlias(local, NamespaceAliasRecord{ModuleIndex{1}, "ns", ModuleIndex{2}, "inner", MapperRange{}, AliasOrigin::Local}));
    ASSERT_TRUE(registry.promoteAliasToRoot(root, NamespaceAliasRecord{ModuleIndex{1}, "outer::ns", ModuleIndex{2}, "inner", MapperRange{}, AliasOrigin::Local}));
    EXPECT_EQ(local.aliasRefs.size(), 1u);
    ASSERT_EQ(root.aliasRefs.size(), 1u);
    EXPECT_EQ(registry.aliasAt(root.aliasRefs.front()).origin, AliasOrigin::PromotedToRoot);
    EXPECT_TRUE(registry.aliasAt(root.aliasRefs.front()).scopeRange.isInvalid());
    // Проекция содержит обе привязки (локальную и корневую), фактов интерфейса здесь нет.
    EXPECT_EQ(registry.aliasProjection().size(), 2u);
}

} // namespace
} // namespace trust
