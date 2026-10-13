// test/unit/types/type_read_view_test.cpp
// Контракт READ-модели: это view над зонами-владельцами, а не второй API.
//   * модель строится из зон (и из корня как capability-narrowing);
//   * write-API и делегирующие read-методы зон НА МОДЕЛИ ОТСУТСТВУЮТ;
//   * getType на неизвестном имени даёт FAULT (без fallback);
//   * forEachType покрывает пользовательский и встроенный домены.
#include "types/names.hpp"
#include "types/type_system.hpp"
#include "types/type_read.hpp"
#include "type_registry_fixture.hpp"

#include "gtest/gtest.h"

#include <stdexcept>
#include <string_view>

namespace trust {
namespace {

// --- Compile-time guard: модель не имеет ни записи, ни делегаций зон ---------------------
template <class T>
concept HasRegisterType = requires(T& t, std::string_view name, TypeId base) { t.registerType(name, base); };
template <class T>
concept HasAddMethod = requires(T& t, TypeId type, std::string_view name, TypeId signature) { t.addMethod(type, name, signature); };
template <class T>
concept HasGetTypeName = requires(const T& t, TypeId id) { t.getTypeName(id); };
template <class T>
concept HasLookup = requires(const T& t, TypeId id) { t.lookup(id); };
template <class T>
concept HasGetTypeDataAs = requires(const T& t, TypeId id) { t.template getTypeDataAs<int>(id); };
template <class T>
concept HasFindMethodInfo = requires(const T& t, TypeId id, std::string_view name) { t.findMethodInfo(id, name); };
template <class T>
concept HasPreprocInclude = requires(const T& t, TypeId id) { t.getPreprocInclude(id); };

static_assert(!HasRegisterType<TypeReadModel>, "read model must not mutate types");
static_assert(!HasAddMethod<TypeReadModel>, "read model must not register methods");
static_assert(!HasGetTypeName<TypeReadModel>, "type name is printed by the printer zone");
static_assert(!HasLookup<TypeReadModel>, "descriptor lookup is the store zone");
static_assert(!HasGetTypeDataAs<TypeReadModel>, "type data is the store zone");
static_assert(!HasFindMethodInfo<TypeReadModel>, "method lookup is the methods zone");
static_assert(!HasPreprocInclude<TypeReadModel>, "includes are the builtins zone");
static_assert(
    requires(const TypeReadModel& m) {
        m.store();
        m.names();
        m.printer();
        m.builtins();
        m.methods();
        m.identity();
    }, "read model exposes the zones it is a view of");

// --- Runtime: view над зонами -----------------------------------------------------------
class TypeReadViewTest : public test::TypeSystemFixture {};

struct NamedTypeCollector {
    bool& sawBuiltin;
    bool& sawUser;

    void operator()(const StorageKey&, std::string_view displayName, bool userDefined) const {
        sawBuiltin = sawBuiltin || (!userDefined && displayName == type::Int32);
        sawUser = sawUser || (userDefined && displayName == "ViewUserType");
    }
};

TEST_F(TypeReadViewTest, ModelIsBuiltFromZones) {
    const TypeId int32 = types().read().getType(type::Int32);
    const TypeReadModel fromZones(types().store(), types().names(), types().printer(), types().builtins(), types().methods(), types().typeCodes());
    EXPECT_EQ(fromZones.getType(type::Int32), int32);
    EXPECT_EQ(fromZones.findType(type::Int32), int32);
    EXPECT_EQ(fromZones.store().descriptorOf(int32)->name, "Int32");
}

TEST_F(TypeReadViewTest, UnknownTypeNameIsFaultWithoutFallback) {
    EXPECT_THROW((void)types().read().getType("NoSuchType"), std::runtime_error);
    EXPECT_FALSE(types().read().findType("NoSuchType").has_value());
}

TEST_F(TypeReadViewTest, ForEachTypeCoversUserAndBuiltinDomains) {
    const TypeId int32 = types().read().getType(type::Int32);
    const TypeId alias = types().registrar().registerType("ViewUserType", int32);
    ASSERT_NE(alias, INVALID_TYPE_ID);

    bool sawBuiltin = false;
    bool sawUser = false;
    types().read().forEachType(NamedTypeCollector{sawBuiltin, sawUser});
    EXPECT_TRUE(sawBuiltin);
    EXPECT_TRUE(sawUser);
}

} // namespace
} // namespace trust
