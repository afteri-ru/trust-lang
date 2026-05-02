#include <gtest/gtest.h>
#include "types/forward.hpp"
#include "types/types.hpp"

// TEST(AliasTest, resolve_alias) {
//     auto& t = trust::Types::instance();
//     EXPECT_EQ(t.resolve_alias(trust::TypeKind::AliasInt), trust::TypeKind::Int32);
//     EXPECT_EQ(t.resolve_alias(trust::TypeKind::AliasLong), trust::TypeKind::Int64);
//     EXPECT_EQ(t.resolve_alias(trust::TypeKind::AliasSizeT), trust::TypeKind::Int64);
//     EXPECT_EQ(t.resolve_alias(trust::TypeKind::AliasUInt), trust::TypeKind::Int64);
// }

// TEST(AliasTest, alias_names) {
//     auto& t = trust::Types::instance();
//     EXPECT_EQ(t.name(trust::TypeKind::AliasInt), "AliasInt");
//     EXPECT_EQ(t.name(trust::TypeKind::AliasLong), "AliasLong");
//     EXPECT_EQ(t.name(trust::TypeKind::AliasSizeT), "AliasSizeT");
//     EXPECT_EQ(t.name(trust::TypeKind::AliasUInt), "AliasUInt");
// }
