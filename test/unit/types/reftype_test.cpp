// test/unit/types/reftype_test.cpp
// Юнит-тесты плоского enum RefType (виды ссылок): маппинг строк, round-trip
// withRefType/getRefType, составной ссылочный узел getOrCreateRefType (вложенность,
// интернирование) и регистрация атрибута reftype.

#include "types/typekind.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "types/registry.hpp"
#include "types/ref_type.hpp"
#include "attrs/attr.hpp"
#include "attrs/attr_builtin.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <string>

namespace trust {
namespace {

TEST(RefTypeTest, StringMapping) {
    // Все 9 мнемонических имён → RefType.
    EXPECT_EQ(refTypeFromString("value"), RefType::kValue);
    EXPECT_EQ(refTypeFromString("shared"), RefType::kShared);
    EXPECT_EQ(refTypeFromString("weak"), RefType::kWeak);
    EXPECT_EQ(refTypeFromString("unique"), RefType::kUnique);
    EXPECT_EQ(refTypeFromString("ptr"), RefType::kPtr);
    EXPECT_EQ(refTypeFromString("mptr"), RefType::kMptr);
    EXPECT_EQ(refTypeFromString("ref"), RefType::kRef);
    EXPECT_EQ(refTypeFromString("rref"), RefType::kRref);
    EXPECT_EQ(refTypeFromString("ptrptr"), RefType::kPtrPtr);
    EXPECT_EQ(refTypeFromString("locker"), RefType::kLocker);
    // Неизвестное имя - nullopt (без тихого fallback).
    EXPECT_EQ(refTypeFromString("raw"), std::nullopt);
    EXPECT_EQ(refTypeFromString(""), std::nullopt);
}

TEST(RefTypeTest, TypeSigilMapping) {
    // Символические сиглы декларации ссылочного типа всегда начинаются с `&`:
    // `&& x` (unique) / `&* x` (shared) / `&? w` (weak). Нативная ось (`ptr`/`ref`) краткой
    // символьной нотации НЕ имеет - только явный `@[reftype("ptr"|"ref")]`.
    EXPECT_EQ(refTypeFromTypeSigil("&&"), RefType::kUnique);
    EXPECT_EQ(refTypeFromTypeSigil("&*"), RefType::kShared);
    EXPECT_EQ(refTypeFromTypeSigil("&?"), RefType::kWeak);
    // Удалённые нативные маркеры - nullopt (без тихого fallback).
    EXPECT_EQ(refTypeFromTypeSigil("%&"), std::nullopt);
    EXPECT_EQ(refTypeFromTypeSigil("%*"), std::nullopt);
    // Одиночные `&`/`*` - операторы address-of/разыменования, не сиглы декларации - nullopt.
    EXPECT_EQ(refTypeFromTypeSigil("&"), std::nullopt);
    EXPECT_EQ(refTypeFromTypeSigil("*"), std::nullopt);
    // Неизвестный сигл - nullopt.
    EXPECT_EQ(refTypeFromTypeSigil(""), std::nullopt);
}

TEST(RefTypeTest, KindClassifiers) {
    // Единые классификаторы вида ссылки (types/typekind.hpp) - заменяют инлайновые сравнения.
    // Нативные (сырые C++): ptr/ref/rref/ptrptr.
    for (RefType k : {RefType::kPtr, RefType::kRef, RefType::kRref, RefType::kPtrPtr}) {
        EXPECT_TRUE(isNativeRefKind(k));
        EXPECT_FALSE(isSmartRefKind(k));
    }
    // Умные (владеющие/охраняемые): shared/weak/unique/locker.
    for (RefType k : {RefType::kShared, RefType::kWeak, RefType::kUnique, RefType::kLocker}) {
        EXPECT_TRUE(isSmartRefKind(k));
        EXPECT_FALSE(isNativeRefKind(k));
    }
    // Ни нативные, ни умные: value/mptr.
    for (RefType k : {RefType::kValue, RefType::kMptr}) {
        EXPECT_FALSE(isNativeRefKind(k));
        EXPECT_FALSE(isSmartRefKind(k));
    }
    // Совместимость заявленного вида с фактическим: носитель без вида совместим с любым.
    EXPECT_TRUE(refKindCompatible(RefType::kShared, RefType::kValue));
    EXPECT_TRUE(refKindCompatible(RefType::kUnique, RefType::kUnique));
    EXPECT_FALSE(refKindCompatible(RefType::kShared, RefType::kUnique));
    EXPECT_FALSE(refKindCompatible(RefType::kPtr, RefType::kRef));
}

TEST(RefTypeTest, DisplayName) {
    // Парный к refTypeCppName: мнемоника вида (диагностики/дампы).
    EXPECT_EQ(refTypeDisplayName(RefType::kValue, "Int32"), "Int32");
    EXPECT_EQ(refTypeDisplayName(RefType::kShared, "Int32"), "shared<Int32>");
    EXPECT_EQ(refTypeDisplayName(RefType::kWeak, "Int32"), "weak<Int32>");
    EXPECT_EQ(refTypeDisplayName(RefType::kUnique, "Int32"), "unique<Int32>");
    EXPECT_EQ(refTypeDisplayName(RefType::kUnique, "Int32", "FreeDeleter"), "unique<Int32, FreeDeleter>");
    EXPECT_EQ(refTypeDisplayName(RefType::kPtr, "Int32"), "ptr<Int32>");
    EXPECT_EQ(refTypeDisplayName(RefType::kMptr, "Int32"), "Int32");
}

TEST(RefTypeTest, NameRoundTrip) {
    for (RefType k : {RefType::kValue, RefType::kShared, RefType::kWeak, RefType::kUnique, RefType::kPtr, RefType::kMptr, RefType::kRef, RefType::kRref,
                      RefType::kPtrPtr, RefType::kLocker}) {
        EXPECT_EQ(refTypeFromString(refTypeName(k)), k);
    }
}

TEST(RefTypeTest, WithGetRefTypeRoundTrip) {
    const TypeKind base = makeTypeKind(Group::kIntegers, 32);
    EXPECT_EQ(getRefType(base), RefType::kValue);
    // Разные виды дают разные TypeKind; round-trip сохраняет вид.
    TypeKind prev = base;
    for (RefType k : {RefType::kShared, RefType::kWeak, RefType::kUnique, RefType::kPtr, RefType::kMptr, RefType::kRef, RefType::kRref, RefType::kPtrPtr,
                      RefType::kLocker}) {
        const TypeKind with = withRefType(base, k);
        EXPECT_EQ(getRefType(with), k);
        EXPECT_NE(with, prev);
        prev = with;
    }
    // Возврат к value.
    EXPECT_EQ(getRefType(withRefType(prev, RefType::kValue)), RefType::kValue);
}

class RefTypeFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(RefTypeFixture, RefTypeNode) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");

    // Первый уровень - узел с видом kPtr над Int32.
    const TypeId p = reg.getOrCreateRefType(RefType::kPtr, int32);
    EXPECT_NE(p, INVALID_TYPE_ID);
    EXPECT_NE(p, int32);
    EXPECT_EQ(getRefType(getKindFromId(p)), RefType::kPtr);
    ASSERT_TRUE(reg.isTypeDataKind(p, TypeDataKind::kRefType));
    const auto* data = reg.getTypeDataAs<RefTypeData>(p);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->pointeeType, int32);

    // Интернирование: одинаковый (вид, pointee) → тот же id.
    EXPECT_EQ(reg.getOrCreateRefType(RefType::kPtr, int32), p);

    // Другой вид над тем же pointee - другой тип.
    EXPECT_NE(reg.getOrCreateRefType(RefType::kShared, int32), p);

    // Вложенность: shared<ptr<Int32>> - отдельный узел, а НЕ перезапись вида.
    const TypeId sp = reg.getOrCreateRefType(RefType::kShared, p);
    EXPECT_NE(sp, p);
    EXPECT_EQ(getRefType(getKindFromId(sp)), RefType::kShared);
    const auto* spData = reg.getTypeDataAs<RefTypeData>(sp);
    ASSERT_NE(spData, nullptr);
    EXPECT_EQ(spData->pointeeType, p); // указывает на узел ptr<Int32>
    // Вложенность интернируется структурно.
    EXPECT_EQ(reg.getOrCreateRefType(RefType::kShared, p), sp);
}

TEST_F(RefTypeFixture, ReftypeAttrRegistered) {
    // Атрибут reftype зарегистрирован как встроенный и принимает строковый параметр.
    auto id = m_ctx.attrs().lookup(attr::Reftype);
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(detail::is_builtin(*id));
    EXPECT_TRUE(m_ctx.attrs().get(*id).has_params());
}

// -- Кодогенерация: эмиссия C++-имени для RefType (getCppTypeName) --
TEST_F(RefTypeFixture, GetCppTypeNameRefKinds) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");

    // Fast-path бит на встроенном типе: суффикс/обёртка вида.
    auto refTyped = [&](TypeId base, RefType rt) { return makeTypeId(withRefType(getKindFromId(base), rt), getIndexFromId(base)); };
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kValue)).value(), "int32_t");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kPtr)).value(), "int32_t*");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kPtrPtr)).value(), "int32_t**");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kRef)).value(), "int32_t&");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kRref)).value(), "int32_t&&");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kShared)).value(), "trust::Shared<int32_t>");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kWeak)).value(), "trust::Weak<trust::Shared<int32_t>>");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kUnique)).value(), "trust::StaticUnique<int32_t>");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kLocker)).value(), "trust::Locker<int32_t>");

    // const + ptr → `const int32_t*` (const применяется к pointee перед суффиксом).
    EXPECT_EQ(reg.getCppTypeName(setFlag(refTyped(int32, RefType::kPtr), SymbolFlag::Const)).value(), "const int32_t*");
}

TEST_F(RefTypeFixture, GetCppTypeNameNestedRef) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");

    // Вложенность: shared<ptr<Int32>> - узел RefTypeData, рекурсивная эмиссия.
    const TypeId p = reg.getOrCreateRefType(RefType::kPtr, int32);
    const TypeId sp = reg.getOrCreateRefType(RefType::kShared, p);
    EXPECT_EQ(reg.getCppTypeName(p).value(), "int32_t*");
    EXPECT_EQ(reg.getCppTypeName(sp).value(), "trust::Shared<int32_t*>");
}

// -- Встроенные политики доступа (Group::kAccessPolicy) --
TEST_F(RefTypeFixture, SyncPoliciesRegistered) {
    TypeRegistry& reg = m_ctx.types();
    for (const char* name : {"AccessMutex", "AccessRwMutex", "AccessSingleThread"}) {
        const auto id = reg.findType(name);
        ASSERT_TRUE(id.has_value()) << name;
        EXPECT_TRUE(reg.isAccessPolicyType(*id)) << name;
        // Признак «тип несёт атрибуты» (kHasAttrsFlag) выставлен у политик sync.
        EXPECT_TRUE(hasAttrsFlag(getKindFromId(*id))) << name;
        // C++-имя из реестра (cppName), НЕ строковый маппинг.
        EXPECT_EQ(reg.getCppTypeName(*id).value(), std::string("trust::") + name);
    }
    // Не-политика - не sync-политика.
    EXPECT_FALSE(reg.isAccessPolicyType(reg.getType("Int32")));
}

TEST_F(RefTypeFixture, SyncPoliciesDistinctViaRegistry) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId a = reg.getType(type::AccessMutex);
    const TypeId b = reg.getType(type::AccessRwMutex);
    // Оба несут kHasAttrsFlag -> сравнение через реестр даёт «различны» (разные политики).
    EXPECT_FALSE(reg.typesEqual(a, b));
    EXPECT_TRUE(reg.typesEqual(a, a));
}

// -- Политики доступа: только для shared/weak; `unique` (монопольное владение) политик не имеет --
TEST_F(RefTypeFixture, AccessPolicyApplicabilityByKind) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId mutex = reg.getType(type::AccessMutex);
    // shared/weak -> sync-политики (AccessMutex/AccessRwMutex/AccessSingleThread).
    EXPECT_TRUE(reg.isAccessPolicyTypeFor(RefType::kShared, mutex));
    EXPECT_TRUE(reg.isAccessPolicyTypeFor(RefType::kWeak, mutex));
    // `unique` монополен - политик не принимает; прочие виды - тем более.
    EXPECT_FALSE(reg.isAccessPolicyTypeFor(RefType::kUnique, mutex));
    EXPECT_FALSE(reg.isAccessPolicyTypeFor(RefType::kPtr, mutex));
}

// -- Класс реализации механизма доступа (3-й аргумент reftype, RefTypeData::implType) --
TEST_F(RefTypeFixture, SharedWithImplIsPartOfType) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");
    const TypeId mutex = reg.getType(type::AccessMutex);
    // Любой зарегистрированный тип как класс реализации (реальная валидация интерфейса - C++-концепт).
    const TypeId impl = reg.getType(type::FreeDeleter);
    const TypeId withImpl = reg.applyRefType(int32, RefType::kShared, INVALID_TYPE_ID, mutex, impl);
    const TypeId noImpl = reg.applyRefType(int32, RefType::kShared, INVALID_TYPE_ID, mutex);
    // impl входит в тип: shared<T,P,I> != shared<T,P>.
    EXPECT_FALSE(reg.typesEqual(withImpl, noImpl));
    EXPECT_EQ(reg.getCppTypeName(noImpl).value(), "trust::AccessShared<int32_t, trust::AccessMutex>");
    EXPECT_EQ(reg.getCppTypeName(withImpl).value(), "trust::AccessShared<int32_t, trust::AccessMutex, trust::FreeDeleter>");
}

// -- Deleter внешнего ресурса (@[deleter]) ------------------
TEST_F(RefTypeFixture, DeleterAttrRegistered) {
    auto id = m_ctx.attrs().lookup(attr::Deleter);
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(detail::is_builtin(*id));
    EXPECT_TRUE(m_ctx.attrs().get(*id).has_params());
}

TEST_F(RefTypeFixture, BuiltinFreeDeleterRegistered) {
    TypeRegistry& reg = m_ctx.types();
    // Встроенные deleter-типы ресурсов (Group::kDeleterPolicy) — из trust/resource.hpp.
    struct DeleterCase {
        const char* name;
        const char* cpp;
    };
    for (const DeleterCase dc : {DeleterCase{"FreeDeleter", "trust::FreeDeleter"}, DeleterCase{"FileDeleter", "trust::FileDeleter"}}) {
        const auto d = reg.findType(dc.name);
        ASSERT_TRUE(d.has_value()) << dc.name;
        EXPECT_TRUE(reg.isDeleterPolicyType(*d)) << dc.name;
        EXPECT_EQ(reg.getCppTypeName(*d).value(), dc.cpp) << dc.name;
    }
    // Обычный тип - не deleter-политика.
    EXPECT_FALSE(reg.isDeleterPolicyType(reg.getType("Int32")));
}

TEST_F(RefTypeFixture, UniqueWithDeleterIsPartOfType) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");
    const TypeId freeD = *reg.findType("FreeDeleter");
    const TypeId myD = reg.registerNativeClass("MyDel", "MyDel", MapperRange{});
    ASSERT_NE(myD, INVALID_TYPE_ID);

    const TypeId b0 = reg.applyRefType(int32, RefType::kUnique);        // unique<Int32>
    const TypeId b1 = reg.applyRefType(int32, RefType::kUnique, freeD); // unique<Int32,FreeDeleter>
    const TypeId b2 = reg.applyRefType(int32, RefType::kUnique, myD);   // unique<Int32,MyDel>
    // Разные D ⇒ разные TypeId (AC3); без D ≠ с D.
    EXPECT_NE(b1, b2);
    EXPECT_NE(b1, b0);
    EXPECT_EQ(getRefType(getKindFromId(b1)), RefType::kUnique);
    const auto* d1 = reg.getTypeDataAs<RefTypeData>(b1);
    ASSERT_NE(d1, nullptr);
    EXPECT_EQ(d1->pointeeType, int32);
    EXPECT_EQ(d1->deleterType, freeD);
    // Интернирование: тот же (pointee,D) → тот же TypeId.
    EXPECT_EQ(reg.applyRefType(int32, RefType::kUnique, freeD), b1);
    // C++-имя: trust::Unique<T, D> (AC4).
    EXPECT_EQ(reg.getCppTypeName(b1).value(), "trust::Unique<int32_t, trust::FreeDeleter>");
    EXPECT_EQ(reg.getCppTypeName(b2).value(), "trust::Unique<int32_t, MyDel>");
}

TEST_F(RefTypeFixture, SharedWithDeleterKeepsSharedType) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");
    // shared D стирается: C++-тип остаётся trust::Shared<T> (кодоген применяет D при adopt).
    EXPECT_EQ(reg.getCppTypeName(reg.applyRefType(int32, RefType::kShared)).value(), "trust::Shared<int32_t>");
}

TEST_F(RefTypeFixture, GetFullTypeNameRefKinds) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");
    const TypeId freeD = *reg.findType("FreeDeleter");
    const TypeId myD = reg.registerNativeClass("MyDel2", "MyDel2", MapperRange{});
    // Обычный тип.
    EXPECT_EQ(reg.getFullTypeName(int32), "Int32");
    // Ссылочные виды - с ВИДОМ (не только базовый тип).
    EXPECT_EQ(reg.getFullTypeName(reg.applyRefType(int32, RefType::kShared)), "shared<Int32>");
    EXPECT_EQ(reg.getFullTypeName(reg.applyRefType(int32, RefType::kUnique)), "unique<Int32>");
    EXPECT_EQ(reg.getFullTypeName(reg.applyRefType(int32, RefType::kPtr)), "ptr<Int32>");
    // borrowerd c deleter: D входит в отображаемый тип.
    EXPECT_EQ(reg.getFullTypeName(reg.applyRefType(int32, RefType::kUnique, freeD)), "unique<Int32, FreeDeleter>");
    EXPECT_EQ(reg.getFullTypeName(reg.applyRefType(int32, RefType::kUnique, myD)), "unique<Int32, MyDel2>");
}

TEST_F(RefTypeFixture, TypesEqualFastPathNoAttrs) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");
    const TypeId int64 = reg.getType("Int64");
    // Fast-path: ни у одного типа нет флага -> сравнение по TypeId (без реестра).
    EXPECT_FALSE(hasAttrsFlag(getKindFromId(int32)));
    EXPECT_TRUE(reg.typesEqual(int32, int32));
    EXPECT_FALSE(reg.typesEqual(int32, int64));
}

TEST_F(RefTypeFixture, HasAttrsFlagSetOnAttrsType) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId base = reg.getType("Int32");
    const auto sync_attr = m_ctx.attrs().lookup(attr::Sync);
    ASSERT_TRUE(sync_attr.has_value());
    // Пользовательский тип с атрибутами получает признак kHasAttrsFlag.
    const TypeId with_attr = reg.registerType("MySync", base, {*sync_attr}, MapperRange{});
    ASSERT_NE(with_attr, INVALID_TYPE_ID);
    EXPECT_TRUE(hasAttrsFlag(getKindFromId(with_attr)));
    // Тип без атрибутов - без флага.
    const TypeId plain = reg.registerType("MyPlain", base, {}, MapperRange{});
    ASSERT_NE(plain, INVALID_TYPE_ID);
    EXPECT_FALSE(hasAttrsFlag(getKindFromId(plain)));
}

// -- ref-вид над АЛИАСОМ: C++-имя pointee - «как написано», вид не теряется канонизацией --
TEST_F(RefTypeFixture, ApplyRefTypeOverAliasKeepsWrittenName) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");
    // Пользовательский алиас `MyInt ::= :Int32`.
    const TypeId myint = reg.registerType("MyInt", int32);
    ASSERT_NE(myint, INVALID_TYPE_ID);

    // ref над алиасом - структурный узел (НЕ fast-path бит): pointee = алиас «как написан».
    const TypeId shared = reg.applyRefType(myint, RefType::kShared);
    EXPECT_NE(shared, myint);
    EXPECT_EQ(getRefType(getKindFromId(shared)), RefType::kShared);
    EXPECT_EQ(reg.getPointeeType(shared), myint);
    // C++-имя pointee - манглинг trust-имени алиаса (`c_MyInt`), а НЕ канонический int32_t.
    EXPECT_EQ(reg.getCppTypeName(shared).value(), "trust::Shared<c_MyInt>");
    // Вид сохраняется в канонизации: shared<MyInt> НЕ равен плоскому MyInt (иначе - потеря типа).
    EXPECT_NE(reg.getCanonicalTypeId(shared), reg.getCanonicalTypeId(myint));

    // Встроенный алиас (`Integer ← Int64`) → канонический C++-тип базового.
    const auto integer = reg.findType("Integer");
    ASSERT_TRUE(integer.has_value());
    EXPECT_EQ(reg.getCppTypeName(reg.applyRefType(*integer, RefType::kShared)).value(), "trust::Shared<int64_t>");

    // unique над алиасом с deleter: D - часть типа, pointee «как написан».
    const auto freeD = reg.findType("FreeDeleter");
    ASSERT_TRUE(freeD.has_value());
    const TypeId bor = reg.applyRefType(myint, RefType::kUnique, *freeD);
    EXPECT_EQ(reg.getCppTypeName(bor).value(), "trust::Unique<c_MyInt, trust::FreeDeleter>");
}

} // namespace
} // namespace trust
