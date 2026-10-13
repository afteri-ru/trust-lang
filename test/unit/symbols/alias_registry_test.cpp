#include "symbols/import/alias_registry.hpp"

#include "gtest/gtest.h"

#include <stdexcept>
#include <vector>

namespace trust {
namespace {

// -- AliasRegistry: единственное хранилище using-областей и namespace-алиасов ----------------

TEST(AliasRegistry, UsingIsRecordedOncePerScope) {
    AliasRegistry registry;
    Scope scope;
    registry.recordUsing(scope, UsingScopeRecord{ModuleIndex{1}, "std", std::nullopt, MapperRange{}, MapperRange{}});
    registry.recordUsing(scope, UsingScopeRecord{ModuleIndex{1}, "std", std::nullopt, MapperRange{}, MapperRange{}});
    ASSERT_EQ(scope.usingRefs.size(), 1u);
    EXPECT_EQ(registry.usingAt(scope.usingRefs.front()).path, "std");

    // Другая модульная цель — отдельная запись.
    registry.recordUsing(scope, UsingScopeRecord{ModuleIndex{1}, "std", ModuleIndex{2}, MapperRange{}, MapperRange{}});
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

TEST(AliasRegistry, ExportedFactsAreProjectedSeparately) {
    AliasRegistry registry;
    const std::vector<ModuleNamespaceAlias> facts{{"outer", ModuleIndex{2}, "inner"}};
    registry.recordExportedFacts(ModuleIndex{1}, facts);
    registry.recordExportedFacts(ModuleIndex{1}, facts); // идемпотентно
    EXPECT_TRUE(registry.hasExportedFacts(ModuleIndex{1}));
    EXPECT_FALSE(registry.hasExportedFacts(ModuleIndex{2}));

    const std::vector<NamespaceAliasInfo> exported = registry.exportedProjection();
    ASSERT_EQ(exported.size(), 1u);
    EXPECT_EQ(exported.front().importer.value, 1u);
    EXPECT_EQ(exported.front().alias, "outer");
    // Факты интерфейса не попадают в общую проекцию локальных привязок.
    EXPECT_TRUE(registry.aliasProjection().empty());
}

TEST(AliasRegistry, ExportedFactConflictIsInternalError) {
    AliasRegistry registry;
    registry.recordExportedFacts(ModuleIndex{1}, std::vector<ModuleNamespaceAlias>{{"outer", ModuleIndex{2}, "inner"}});
    // Тот же префикс того же владельца, но другая цель — язык такое запрещает: это ошибка данных,
    // а не «первый победитель».
    EXPECT_THROW(registry.recordExportedFacts(ModuleIndex{1}, std::vector<ModuleNamespaceAlias>{{"outer", ModuleIndex{3}, "other"}}),
                 std::runtime_error);
    // Невалидный факт (пустой префикс) — тоже ошибка данных.
    EXPECT_THROW(registry.recordExportedFacts(ModuleIndex{1}, std::vector<ModuleNamespaceAlias>{{"", ModuleIndex{2}, "inner"}}), std::runtime_error);
    EXPECT_THROW(registry.recordExportedFacts(ModuleIndex{1}, std::vector<ModuleNamespaceAlias>{{"outer", ModuleIndex::invalid(), "inner"}}),
                 std::runtime_error);
}

TEST(AliasRegistry, LongestExportedAliasMatchesPrefix) {
    AliasRegistry registry;
    const std::vector<ModuleNamespaceAlias> facts{{"outer", ModuleIndex{2}, "inner"}, {"outer::deep", ModuleIndex{3}, "leaf"}};
    registry.recordExportedFacts(ModuleIndex{1}, facts);

    std::string_view rest;
    const NamespaceAliasRecord* best = registry.longestExportedAlias(ModuleIndex{1}, "outer::deep::x", &rest);
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->alias, "outer::deep");
    EXPECT_EQ(rest, "x");
    EXPECT_EQ(best->targetModule.value, 3u);

    // Не-префиксное совпадение не принимается.
    EXPECT_EQ(registry.longestExportedAlias(ModuleIndex{1}, "outerx::y", &rest), nullptr);
}

} // namespace
} // namespace trust
