// test/unit/types/type_write_access_test.cpp
// Контракт записи типовой системы: единственная точка входа — зона-владелец.
// Корень (`TypeSystem`) не переадресует запись; чтение — через const-модель.
#include "types/names.hpp"
#include "types/type_system.hpp"
#include "types/type_read.hpp"
#include "type_registry_fixture.hpp"

#include "gtest/gtest.h"

#include <string_view>

namespace trust {
namespace {

// --- Compile-time guard: write-API доступен только у зоны-владельца --------------------
template <class T>
concept HasRegisterType = requires(T& t, std::string_view name, TypeId base) { t.registerType(name, base); };
template <class T>
concept HasAddMethod = requires(T& t, TypeId type, std::string_view name, TypeId signature) { t.addMethod(type, name, signature); };
template <class T>
concept HasAliasTypeName = requires(T& t, TypeId target, const StorageKey& key, std::string_view name, MapperRange range) {
    t.aliasTypeName(target, key, name, range);
};
template <class T>
concept HasAliasKey = requires(T& t, TypeId id, const StorageKey& key) { t.aliasKey(id, key); };

static_assert(HasRegisterType<NamedTypeRegistrar>, "NamedTypeRegistrar owns named-type creation");
static_assert(HasAddMethod<MethodTable>, "MethodTable owns method registration");
static_assert(HasAliasTypeName<TypeNameIndex>, "TypeNameIndex owns second-name registration");
static_assert(HasAliasKey<TypeNameIndex>, "TypeNameIndex owns visibility-key aliasing");

static_assert(!HasRegisterType<TypeSystem>, "root must not re-expose registerType");
static_assert(!HasAddMethod<TypeSystem>, "root must not re-expose addMethod");
static_assert(!HasAliasTypeName<TypeSystem>, "root must not re-expose aliasTypeName");
static_assert(!HasAliasKey<TypeSystem>, "root must not re-expose aliasKey");
static_assert(!HasRegisterType<TypeReadModel>, "read model must not mutate types");

// --- Runtime: каждая запись идёт через свою зону ---------------------------------------
class TypeWriteAccessTest : public test::TypeSystemFixture {};

TEST_F(TypeWriteAccessTest, RegistrarOwnsNamedTypeCreation) {
    const TypeId int32 = model().getType(type::Int32);
    const TypeId alias = types().registrar().registerType("MyInt", int32);
    ASSERT_NE(alias, INVALID_TYPE_ID);
    EXPECT_EQ(model().findType("MyInt"), alias);
    EXPECT_EQ(model().store().descriptorOf(alias)->baseType, int32);
}

TEST_F(TypeWriteAccessTest, MethodsZoneOwnsMethodRegistration) {
    const TypeId int32 = model().getType(type::Int32);
    // addMethod работает только на НЕ-алиасных пользовательских типах: алиас десугарится до
    // базового (встроенного) типа, а встроенные дескрипторы иммутабельны.
    const TypeId owner = types().registrar().registerNativeClass("MethodOwner", "MethodOwnerCpp", {});
    ASSERT_NE(owner, INVALID_TYPE_ID);
    const TypeId signature = types().interner().getOrCreateFunctionType(int32, {int32});
    types().methods().addMethod(owner, "twice", signature);

    const auto found = types().methods().findMethodInfo(owner, "twice");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->key, "twice");
    ASSERT_EQ(found->entries.size(), 1U);
    EXPECT_EQ(found->entries.front().signature, signature);
}

TEST_F(TypeWriteAccessTest, NamesZoneOwnsSecondName) {
    const TypeId int32 = model().getType(type::Int32);
    const StorageKey key = exportedKey("ns", "SecondName");
    EXPECT_EQ(types().names().aliasTypeName(int32, key, "SecondName", {}), int32);
    EXPECT_EQ(model().findType(key), int32);
    EXPECT_EQ(model().store().descriptorOf(int32)->name, "Int32"); // display-имя остаётся у цели
}

TEST_F(TypeWriteAccessTest, NamesZoneOwnsVisibilityKeyAliasing) {
    const TypeId int32 = model().getType(type::Int32);
    const TypeId alias = types().registrar().registerType("SiteAlias", int32);
    ASSERT_NE(alias, INVALID_TYPE_ID);

    const StorageKey siteKey = exportedKey("site", "SiteAlias");
    types().names().aliasKey(alias, siteKey);
    EXPECT_EQ(model().findType(siteKey), alias);
}

} // namespace
} // namespace trust
