// test/unit/types/type_intern_key_test.cpp
// Исчерпывающий TypeKey::tag (structuralKeyTag): поля идентичности, которые не являются
// type-детьми (ArrayTypeData::isVariadic, TemplateTypeData::templateTypeId), входят в ключ.
#include "type_registry_fixture.hpp"
#include "types/group.hpp"
#include "types/names.hpp"
#include "types/type_system.hpp"
#include "types/typekind.hpp"

#include "gtest/gtest.h"

#include <vector>

namespace trust {
namespace {

class TypeInternKeyTest : public test::TypeSystemFixture {};

TEST_F(TypeInternKeyTest, NonTypeIdentityFieldsArePartOfKey) {
    TypeSystem& reg = types();
    const TypeId i32 = reg.read().getType(type::Int32);

    // Флаг вариадичности массива — поле идентичности, а не type-ребёнок.
    const TypeKind arrayKind = makeTypeKind(Group::kContainers, 1);
    const TypeId fixed = reg.interner().getOrCreateStructuralType("", arrayKind, {TypeKeyChild{i32}}, ArrayTypeData{i32, {}, false});
    const TypeId variadic = reg.interner().getOrCreateStructuralType("", arrayKind, {TypeKeyChild{i32}}, ArrayTypeData{i32, {}, true});
    EXPECT_NE(fixed, variadic);
    EXPECT_EQ(fixed, reg.interner().getOrCreateStructuralType("", arrayKind, {TypeKeyChild{i32}}, ArrayTypeData{i32, {}, false}));

    // Маркер шаблона (templateTypeId) — тоже поле идентичности.
    const TypeKind templateKind = makeTypeKind(Group::kRanges, 1);
    const TypeId range = reg.interner().getOrCreateStructuralType("", templateKind, {TypeKeyChild{i32}}, TemplateTypeData{reg.read().getType(type_category::Range), {i32}});
    const TypeId other = reg.interner().getOrCreateStructuralType("", templateKind, {TypeKeyChild{i32}}, TemplateTypeData{reg.read().getType(type::Array), {i32}});
    EXPECT_NE(range, other);
}

} // namespace
} // namespace trust
