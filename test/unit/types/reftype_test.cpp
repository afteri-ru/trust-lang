// test/unit/types/reftype_test.cpp
// Юнит-тесты плоского enum RefType (виды ссылок): маппинг строк, round-trip
// withRefType/getRefType, составной ссылочный узел getOrCreateRefType (вложенность,
// интернирование) и регистрация атрибута reftype.

#include "types/typekind.hpp"
#include "types/type_id.hpp"
#include "types/registry.hpp"
#include "ast/attr.hpp"
#include "ast/attr_builtin.hpp"
#include "diag/context.hpp"
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
    EXPECT_EQ(refTypeFromString("take"), RefType::kTake);
    // Неизвестное имя - nullopt (без тихого fallback).
    EXPECT_EQ(refTypeFromString("raw"), std::nullopt);
    EXPECT_EQ(refTypeFromString(""), std::nullopt);
}

TEST(RefTypeTest, NameRoundTrip) {
    for (RefType k : {RefType::kValue, RefType::kShared, RefType::kWeak, RefType::kUnique, RefType::kPtr, RefType::kMptr, RefType::kRef, RefType::kRref,
                      RefType::kPtrPtr, RefType::kTake}) {
        EXPECT_EQ(refTypeFromString(refTypeName(k)), k);
    }
}

TEST(RefTypeTest, WithGetRefTypeRoundTrip) {
    const TypeKind base = makeTypeKind(Group::kIntegers, 32);
    EXPECT_EQ(getRefType(base), RefType::kValue);
    // Разные виды дают разные TypeKind; round-trip сохраняет вид.
    TypeKind prev = base;
    for (RefType k :
         {RefType::kShared, RefType::kWeak, RefType::kUnique, RefType::kPtr, RefType::kMptr, RefType::kRef, RefType::kRref, RefType::kPtrPtr, RefType::kTake}) {
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
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kShared)).value(), "std::shared_ptr<int32_t>");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kWeak)).value(), "std::weak_ptr<int32_t>");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kUnique)).value(), "std::unique_ptr<int32_t>");
    EXPECT_EQ(reg.getCppTypeName(refTyped(int32, RefType::kTake)).value(), "trust::Take<int32_t>");

    // const + ptr → `const int32_t*` (const применяется к pointee перед суффиксом).
    EXPECT_EQ(reg.getCppTypeName(withConst(refTyped(int32, RefType::kPtr))).value(), "const int32_t*");
}

TEST_F(RefTypeFixture, GetCppTypeNameNestedRef) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType("Int32");

    // Вложенность: shared<ptr<Int32>> - узел RefTypeData, рекурсивная эмиссия.
    const TypeId p = reg.getOrCreateRefType(RefType::kPtr, int32);
    const TypeId sp = reg.getOrCreateRefType(RefType::kShared, p);
    EXPECT_EQ(reg.getCppTypeName(p).value(), "int32_t*");
    EXPECT_EQ(reg.getCppTypeName(sp).value(), "std::shared_ptr<int32_t*>");
}

} // namespace
} // namespace trust
