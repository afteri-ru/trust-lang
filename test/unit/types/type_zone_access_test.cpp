// test/unit/types/type_zone_access_test.cpp
// Контракт модели доступа к зонам состояния TypeSystem:
//   * примитивы записи TypeStore/TypeNameIndex НЕ публичны (private + friend);
//   * lifecycle-сеттеры зон состояния и seed-регистрация встроенных алиасов НЕ публичны;
//   * адреса TypeDescriptor стабильны при росте хранилища (std::deque).
#include "types/type_system.hpp"
#include "types/registry/builtin_registry.hpp"
#include "types/registry/structural_interner.hpp"
#include "types/registry/type_name_index.hpp"
#include "types/registry/type_store.hpp"

#include "session/context.hpp"
#include "types/names.hpp"
#include "types/type_data.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace {

// --- Compile-time guard: запись не доступна вне друзей --------------------------------
template <class T>
concept HasPublicPush = requires { &T::push; };
template <class T>
concept HasPublicWritableDescriptor = requires { &T::writableDescriptor; };
template <class T>
concept HasPublicSetDeclaredRecord = requires { &T::setDeclaredRecord; };
template <class T>
concept HasPublicTakeDeclaredRecord = requires { &T::takeDeclaredRecord; };
template <class T>
concept HasPublicResetToBuiltin = requires { &T::resetToBuiltin; };
template <class T>
concept HasPublicTakeDescriptors = requires { &T::takeDescriptors; };
template <class T>
concept HasPublicBind = requires { &T::bind; };
template <class T>
concept HasPublicReserve = requires { &T::reserve; };
template <class T>
concept HasPublicAliasTypeName = requires { &T::aliasTypeName; };
template <class T>
concept HasPublicSetBuiltinCore = requires { &T::setBuiltinCore; };
template <class T>
concept HasPublicAliasKey = requires { &T::aliasKey; };
template <class T>
concept HasPublicClearUser = requires { &T::clearUser; };
template <class T>
concept HasPublicTakeUserMap = requires { &T::takeUserMap; };
template <class T>
concept HasPublicRegistrationKey = requires { &T::registrationKey; };
template <class T>
concept HasPublicReset = requires { &T::reset; };
template <class T>
concept HasPublicSetCore = requires { &T::setCore; };

// --- Compile-time guard: у корня нет write-переадресаций (запись — только через зону) ---
template <class T>
concept HasPublicRegisterType = requires(T& t, std::string_view name, TypeId base) { t.registerType(name, base); };
template <class T>
concept HasPublicAddMethod = requires(T& t, TypeId type, std::string_view name, TypeId signature) { t.addMethod(type, name, signature); };
template <class T>
concept HasPublicAliasKeyOnRoot = requires(T& t, TypeId id, const StorageKey& key) { t.aliasTypeKey(id, key); };

static_assert(!HasPublicPush<TypeStore>, "TypeStore::push must be private (friend-only)");
static_assert(!HasPublicWritableDescriptor<TypeStore>, "TypeStore::writableDescriptor must be private (friend-only)");
static_assert(!HasPublicSetDeclaredRecord<TypeStore>, "TypeStore::setDeclaredRecord must be private (friend-only)");
static_assert(!HasPublicTakeDeclaredRecord<TypeStore>, "TypeStore::takeDeclaredRecord must be private (friend-only)");
static_assert(!HasPublicResetToBuiltin<TypeStore>, "TypeStore::resetToBuiltin must be private (friend-only)");
static_assert(!HasPublicTakeDescriptors<TypeStore>, "TypeStore::takeDescriptors must be private (friend-only)");
static_assert(!HasPublicBind<TypeNameIndex>, "TypeNameIndex::bind must be private (friend-only)");
static_assert(!HasPublicReserve<TypeNameIndex>, "TypeNameIndex::reserve must be private (friend-only)");
static_assert(HasPublicAliasTypeName<TypeNameIndex>, "TypeNameIndex::aliasTypeName is the zone-owned write entry");
static_assert(!HasPublicSetBuiltinCore<TypeNameIndex>, "TypeNameIndex::setBuiltinCore must be private (friend-only)");
static_assert(HasPublicAliasKey<TypeNameIndex>, "TypeNameIndex::aliasKey is the zone-owned write entry");
static_assert(!HasPublicClearUser<TypeNameIndex>, "TypeNameIndex::clearUser must be private (friend-only)");
static_assert(!HasPublicTakeUserMap<TypeNameIndex>, "TypeNameIndex::takeUserMap must be private (friend-only)");
static_assert(!HasPublicRegistrationKey<TypeNameIndex>, "TypeNameIndex::registrationKey must be private (friend-only)");
static_assert(HasPublicReset<StructuralInterner>, "StructuralInterner::reset is the zone-owned seed API (composition root only)");
static_assert(!HasPublicSetCore<BuiltinRegistry>, "BuiltinRegistry::setCore must be private (TypeSystem-only)");

// Запись не переадресуется корнем: единственная точка входа — зона-владелец.
static_assert(!HasPublicRegisterType<TypeSystem>, "TypeSystem must not re-expose registerType (NamedTypeRegistrar owns it)");
static_assert(!HasPublicAddMethod<TypeSystem>, "TypeSystem must not re-expose addMethod (MethodTable owns it)");
static_assert(!HasPublicAliasKeyOnRoot<TypeSystem>, "TypeSystem must not re-expose aliasTypeKey (TypeNameIndex owns it)");

// --- Compile-time guard: хранилище формы record не адресуется напрямую (R1/E1) ---------
// Снаружи `types` форма читается ТОЛЬКО аксессорами TypeStore (fieldsOf/basesOf/templateParamsOf/
// instanceOf); поля payload'а приватны, поэтому попытка прямой адресации — ошибка компиляции.
template <class T>
concept HasPublicRecordFields = requires(T data) { data.fields; };
template <class T>
concept HasPublicRecordTemplateParams = requires(T data) { data.templateParams; };
template <class T>
concept HasPublicRecordTemplateOf = requires(T data) { data.templateOf; };
template <class T>
concept HasPublicRecordTemplateArgs = requires(T data) { data.templateArgs; };

static_assert(!HasPublicRecordFields<RecordTypeData>, "RecordTypeData::fields must be private (owner-only accessor fieldsOf)");
static_assert(!HasPublicRecordTemplateParams<RecordTypeData>, "RecordTypeData::templateParams must be private (owner-only accessor templateParamsOf)");
static_assert(!HasPublicRecordTemplateOf<RecordTypeData>, "RecordTypeData::templateOf must be private (owner-only accessor instanceOf)");
static_assert(!HasPublicRecordTemplateArgs<RecordTypeData>, "RecordTypeData::templateArgs must be private (owner-only accessor instanceOf)");

TEST(TypeZoneAccess, WritePrimitivesAreNotPublic) {
    // Проверка зафиксирована static_assert выше; тест фиксирует намерение в отчёте CTest.
    EXPECT_TRUE(true);
}

// --- Runtime: адрес дескриптора стабилен при росте хранилища ---------------------------
class TypeZoneAccessTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(TypeZoneAccessTest, DescriptorDataStaysValidAcrossGrowth) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId alias = reg.registrar().registerType("StableAlias", int32);
    EXPECT_EQ(reg.read().store().nameOf(alias), "StableAlias");
    EXPECT_EQ(reg.read().store().baseTypeOf(alias), int32);

    // Рост хранилища не инвалидирует ранее выданные дескрипторы: данные читаются тем же аксессором.
    for (int i = 0; i < 512; ++i) {
        const TypeId id = reg.registrar().registerType("GrowthAlias" + std::to_string(i), int32);
        ASSERT_NE(id, INVALID_TYPE_ID);
    }
    EXPECT_EQ(reg.read().store().nameOf(alias), "StableAlias");
    EXPECT_EQ(reg.read().store().baseTypeOf(alias), int32);
}

TEST_F(TypeZoneAccessTest, DuplicateTypeNameLeavesNoOrphanDescriptor) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId first = reg.registrar().registerType("DupTx", int32);
    ASSERT_NE(first, INVALID_TYPE_ID);

    // Дубликат отвергается ДО push: осиротевший дескриптор/TypeId не создаётся.
    const TypeId duplicate = reg.registrar().registerType("DupTx", int32);
    EXPECT_EQ(duplicate, INVALID_TYPE_ID);

    const TypeId next = reg.registrar().registerType("DupTxNext", int32);
    ASSERT_NE(next, INVALID_TYPE_ID);
    EXPECT_EQ(getRegistryIndex(next), getRegistryIndex(first) + 1);
    ASSERT_TRUE(reg.read().store().hasDescriptor(first));
    EXPECT_EQ(reg.read().store().nameOf(first), "DupTx");
}

TEST_F(TypeZoneAccessTest, DeclareThenDefineKeepsSameTypeId) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId declared = reg.registrar().declareRecord("TxPoint", Group::kStructs);
    ASSERT_NE(declared, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().store().isForwardDecl(declared));

    const TypeId defined = reg.registrar().defineRecord("TxPoint", Group::kStructs, {{"x", QualType::fromTypeId(int32)}});
    EXPECT_EQ(defined, declared);
    EXPECT_TRUE(reg.read().store().isCompleteType(defined));
    ASSERT_NE(reg.read().store().fieldsOf(defined), nullptr);
    ASSERT_EQ(reg.read().store().fieldsOf(defined)->size(), 1u);
    EXPECT_EQ(reg.read().store().fieldsOf(defined)->front().name, "x");
}

TEST_F(TypeZoneAccessTest, FailedDefineDoesNotConsumeDeclaration) {
    TypeSystem& reg = *m_types;
    const TypeId declared = reg.registrar().declareRecord("TxCls", Group::kStructs);
    ASSERT_NE(declared, INVALID_TYPE_ID);

    // Неверная группа: диагностика, состояние не полуприменено, маркер объявления сохранён.
    const TypeId rejected = reg.registrar().defineRecord("TxCls", Group::kClassDefs, {});
    EXPECT_EQ(rejected, INVALID_TYPE_ID);

    const TypeId defined = reg.registrar().defineRecord("TxCls", Group::kStructs, {});
    EXPECT_EQ(defined, declared);
    EXPECT_TRUE(reg.read().store().isCompleteType(defined));
}

TEST_F(TypeZoneAccessTest, FieldTypeIsOwnedByTheForm) {
    TypeSystem& reg = *m_types;
    const TypeId i64 = reg.read().getType("Int64");
    const TypeId f64 = reg.read().getType("Float64");
    ASSERT_NE(i64, INVALID_TYPE_ID);
    ASSERT_NE(f64, INVALID_TYPE_ID);

    // Тип поля объявленного record: ЕДИНСТВЕННЫЙ владелец — форма (`types`). Пишется `defineRecord`.
    const TypeId rec = reg.registrar().defineRecord("OwnerPoint", Group::kStructs, {{"a", QualType::fromTypeId(i64)}, {"b", QualType::fromTypeId(f64)}});
    ASSERT_NE(rec, INVALID_TYPE_ID);

    // Потребитель читает тип поля ТОЛЬКО через аксессор формы (`fieldsOf`); порядок — порядок объявления.
    const std::vector<TupleElementData>* fields = reg.read().store().fieldsOf(rec);
    ASSERT_NE(fields, nullptr);
    ASSERT_EQ(fields->size(), 2u);
    EXPECT_EQ(fields->at(0).name, "a");
    EXPECT_EQ(fields->at(0).type.getTypeId(), i64);
    EXPECT_EQ(fields->at(1).name, "b");
    EXPECT_EQ(fields->at(1).type.getTypeId(), f64);
}

} // namespace
} // namespace trust
