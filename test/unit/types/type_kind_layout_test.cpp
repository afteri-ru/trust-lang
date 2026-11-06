// test/unit/types/type_kind_layout_test.cpp
// E14: TypeKind — единое определение идентичности/классификации типа в ABI-заголовке
// (runtime/trust/type_state.hpp через types/typekind.hpp). Проверяется контракт битовой упаковки
// (Group/Data/RefType/TypeClass/SizeUnit) — единственная точка, читающая ось TypeClass.
#include "types/typekind.hpp"

#include <gtest/gtest.h>

namespace trust {
namespace {

TEST(TypeKindLayout, RoundTripsGroupDataRefClassSize) {
    const TypeKind k = makeTypeKind(Group::kIntegers, 32, TypeClass::kComplex, RefType::kShared, SizeUnit::kBits);
    EXPECT_EQ(typeGroupOf(k), Group::kIntegers);
    EXPECT_EQ(typeDataOf(k), 32);
    EXPECT_EQ(typeClassOf(k), TypeClass::kComplex);
    EXPECT_EQ(typeRefOf(k), RefType::kShared);
    EXPECT_EQ(typeSizeUnitOf(k), SizeUnit::kBits);
    EXPECT_TRUE(isBuiltinConcrete(k));

    // Оси независимы: смена RefType не трогает Group/Data/TypeClass.
    const TypeKind r = withRefType(k, RefType::kValue);
    EXPECT_EQ(typeGroupOf(r), Group::kIntegers);
    EXPECT_EQ(typeDataOf(r), 32);
    EXPECT_EQ(typeClassOf(r), TypeClass::kComplex);
    EXPECT_EQ(typeRefOf(r), RefType::kValue);
}

} // namespace
} // namespace trust
