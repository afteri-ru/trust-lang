// test/unit/types/array_type_test.cpp
// Юнит-тесты структурного типа массива `Array<Elem>`: интернирование по (elementType, dims),
// аксессоры (isArrayType/arrayElementType/arrayDimensions), константность как kConstFlag-бит
// TypeId (withConst/typeIsConst), методы, многомерность (isMultiDimArray).

#include "types/type_system.hpp"
#include "type_flag_test_utils.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class ArrayTypeFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(ArrayTypeFixture, GetOrCreateArrayType_Interning) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId float64 = reg.read().getType(type::Float64);
    ASSERT_NE(int32, INVALID_TYPE_ID);

    // Одинаковые (elem, dims) → один и тот же TypeId (const - бит, не структурная часть).
    const TypeId a1 = reg.interner().getOrCreateArrayType(int32, {3});
    const TypeId a2 = reg.interner().getOrCreateArrayType(int32, {3});
    EXPECT_EQ(a1, a2);

    // Разный элемент → разный тип.
    EXPECT_NE(reg.interner().getOrCreateArrayType(float64, {3}), a1);
    // Разная размерность → разный тип.
    EXPECT_NE(reg.interner().getOrCreateArrayType(int32, {4}), a1);
    // Константность - отдельный state-бит QualType (withConst). TypeId — identity-value:
    // const-вариант равен структурному массиву, различается только raw().
    const TypeId constA1 = setFlag(a1, TypeFlag::Const);
    EXPECT_EQ(constA1, a1);
    EXPECT_NE(constA1.raw(), a1.raw());
    EXPECT_EQ(reg.read().store().desugaredTypeId(constA1), a1);
}

TEST_F(ArrayTypeFixture, ArrayAccessors) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId arr = reg.interner().getOrCreateArrayType(int32, {3});
    ASSERT_NE(arr, INVALID_TYPE_ID);

    EXPECT_TRUE(reg.read().store().isArrayType(arr));
    EXPECT_EQ(reg.read().store().arrayElementType(arr), int32);
    EXPECT_EQ(reg.read().store().arrayDimensions(arr), (std::vector<uint64_t>{3}));
    EXPECT_FALSE(testFlag(arr, TypeFlag::Const));

    // Константная форма `:Array^` - kConstFlag-бит в TypeId (withConst), а не поле типа.
    const TypeId constArr = setFlag(reg.interner().getOrCreateArrayType(int32, {5}), TypeFlag::Const);
    EXPECT_TRUE(reg.read().store().isArrayType(constArr));
    EXPECT_TRUE(testFlag(constArr, TypeFlag::Const));
    EXPECT_EQ(reg.read().store().arrayDimensions(constArr), (std::vector<uint64_t>{5}));

    // Не-массив → аксессоры безопасны (INVALID/пусто/false).
    EXPECT_FALSE(reg.read().store().isArrayType(int32));
    EXPECT_EQ(reg.read().store().arrayElementType(int32), INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().store().arrayDimensions(int32).empty());
}

TEST_F(ArrayTypeFixture, ArrayDynamicDims) {
    TypeSystem& reg = *m_types;
    const TypeId int64 = reg.read().getType(type::Int64);
    // Пустые размерности = динамический (std::vector), без константности.
    const TypeId dyn = reg.interner().getOrCreateArrayType(int64, {});
    ASSERT_NE(dyn, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().store().isArrayType(dyn));
    EXPECT_TRUE(reg.read().store().arrayDimensions(dyn).empty());
    EXPECT_FALSE(testFlag(dyn, TypeFlag::Const));
}

TEST_F(ArrayTypeFixture, ArrayMethodsRegistered) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId arr = reg.interner().getOrCreateArrayType(int32, {3});
    // Методы объявлены на абстрактном `:Array` и находятся через fallback (findMethodInfo).
    const auto methodSig = [&](std::string_view n) {
        const auto m = reg.read().methods().findMethodInfo(arr, n);
        return m ? m->entries.front().signature : INVALID_TYPE_ID;
    };
    EXPECT_NE(methodSig("count"), INVALID_TYPE_ID);
    EXPECT_NE(methodSig("size"), INVALID_TYPE_ID);
    EXPECT_NE(methodSig("empty"), INVALID_TYPE_ID);
    EXPECT_NE(methodSig("at"), INVALID_TYPE_ID);
    EXPECT_NE(methodSig("first"), INVALID_TYPE_ID);
    // Элемент-зависимый метод `at` подставляет элементный тип.
    const TypeId at = methodSig("at");
    const TypeId inst = reg.interner().instantiateArrayMethod(arr, at);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(inst);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, int32);
}

TEST_F(ArrayTypeFixture, MultiDimArrayDetection) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType(type::Int32);
    // 1D: не многомерный.
    const TypeId one = reg.interner().getOrCreateArrayType(int32, {3});
    EXPECT_FALSE(isMultiDimArray(one, reg));
    // Многомерное определение `:Int32[3,4]` (несколько размерностей).
    const TypeId two = reg.interner().getOrCreateArrayType(int32, {3, 4});
    EXPECT_TRUE(isMultiDimArray(two, reg));
    // Вложенный литерал `[[1,2],[3,4]]`: элемент - сам массив → многомерный.
    const TypeId inner = reg.interner().getOrCreateArrayType(int32, {2});
    const TypeId nested = reg.interner().getOrCreateArrayType(inner, {2});
    EXPECT_TRUE(isMultiDimArray(nested, reg));
    // Скаляр - не массив.
    EXPECT_FALSE(isMultiDimArray(int32, reg));
}

// Identity массива включает размерности (ArrayShapeTag), а не строковое кодирование:
// повтор -> тот же TypeId, другой порядок/набор/динамический -> другой TypeId.
TEST_F(ArrayTypeFixture, DimensionsArePartOfIdentity) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId fixed = reg.interner().getOrCreateArrayType(int32, {3, 4});
    EXPECT_EQ(reg.interner().getOrCreateArrayType(int32, {3, 4}), fixed);
    EXPECT_NE(reg.interner().getOrCreateArrayType(int32, {4, 3}), fixed);
    EXPECT_NE(reg.interner().getOrCreateArrayType(int32, {3}), fixed);
    EXPECT_NE(reg.interner().getOrCreateArrayType(int32, {}), fixed);
}

} // namespace
} // namespace trust
