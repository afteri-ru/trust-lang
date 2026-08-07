// test/unit/types/array_type_test.cpp
// Юнит-тесты структурного типа массива `Array<Elem>`: интернирование по (elementType, dims),
// аксессоры (isArrayType/arrayElementType/arrayDimensions), константность как kConstFlag-бит
// TypeId (withConst/typeIsConst), методы, многомерность (isMultiDimArray).

#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "diag/context.hpp"
#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class ArrayTypeFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(ArrayTypeFixture, GetOrCreateArrayType_Interning) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType(type::Int32);
    const TypeId float64 = reg.getType(type::Float64);
    ASSERT_NE(int32, INVALID_TYPE_ID);

    // Одинаковые (elem, dims) → один и тот же TypeId (const — бит, не структурная часть).
    const TypeId a1 = reg.getOrCreateArrayType(int32, {3});
    const TypeId a2 = reg.getOrCreateArrayType(int32, {3});
    EXPECT_EQ(a1, a2);

    // Разный элемент → разный тип.
    EXPECT_NE(reg.getOrCreateArrayType(float64, {3}), a1);
    // Разная размерность → разный тип.
    EXPECT_NE(reg.getOrCreateArrayType(int32, {4}), a1);
    // Константность — отдельный бит kConstFlag в TypeId (withConst), структурно тот же массив.
    const TypeId constA1 = withConst(a1);
    EXPECT_NE(constA1, a1);
    EXPECT_EQ(reg.getCanonicalTypeId(constA1), a1);
}

TEST_F(ArrayTypeFixture, ArrayAccessors) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType(type::Int32);
    const TypeId arr = reg.getOrCreateArrayType(int32, {3});
    ASSERT_NE(arr, INVALID_TYPE_ID);

    EXPECT_TRUE(reg.isArrayType(arr));
    EXPECT_EQ(reg.arrayElementType(arr), int32);
    EXPECT_EQ(reg.arrayDimensions(arr), (std::vector<uint64_t>{3}));
    EXPECT_FALSE(typeIsConst(arr));

    // Константная форма `:Array^` — kConstFlag-бит в TypeId (withConst), а не поле типа.
    const TypeId constArr = withConst(reg.getOrCreateArrayType(int32, {5}));
    EXPECT_TRUE(reg.isArrayType(constArr));
    EXPECT_TRUE(typeIsConst(constArr));
    EXPECT_EQ(reg.arrayDimensions(constArr), (std::vector<uint64_t>{5}));

    // Не-массив → аксессоры безопасны (INVALID/пусто/false).
    EXPECT_FALSE(reg.isArrayType(int32));
    EXPECT_EQ(reg.arrayElementType(int32), INVALID_TYPE_ID);
    EXPECT_TRUE(reg.arrayDimensions(int32).empty());
}

TEST_F(ArrayTypeFixture, ArrayDynamicDims) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int64 = reg.getType(type::Int64);
    // Пустые размерности = динамический (std::vector), без константности.
    const TypeId dyn = reg.getOrCreateArrayType(int64, {});
    ASSERT_NE(dyn, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.isArrayType(dyn));
    EXPECT_TRUE(reg.arrayDimensions(dyn).empty());
    EXPECT_FALSE(typeIsConst(dyn));
}

TEST_F(ArrayTypeFixture, ArrayMethodsRegistered) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType(type::Int32);
    const TypeId arr = reg.getOrCreateArrayType(int32, {3});
    // Методы объявлены на абстрактном `:Array` и находятся через fallback (findMethodInfo).
    EXPECT_NE(reg.findMethod(arr, "count"), INVALID_TYPE_ID);
    EXPECT_NE(reg.findMethod(arr, "size"), INVALID_TYPE_ID);
    EXPECT_NE(reg.findMethod(arr, "empty"), INVALID_TYPE_ID);
    EXPECT_NE(reg.findMethod(arr, "at"), INVALID_TYPE_ID);
    EXPECT_NE(reg.findMethod(arr, "first"), INVALID_TYPE_ID);
    // Элемент-зависимый метод `at` подставляет элементный тип.
    const TypeId at = reg.findMethod(arr, "at");
    const TypeId inst = reg.instantiateArrayMethod(arr, at);
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(inst);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, int32);
}

TEST_F(ArrayTypeFixture, MultiDimArrayDetection) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId int32 = reg.getType(type::Int32);
    // 1D: не многомерный.
    const TypeId one = reg.getOrCreateArrayType(int32, {3});
    EXPECT_FALSE(isMultiDimArray(one, reg));
    // Многомерное определение `:Int32[3,4]` (несколько размерностей).
    const TypeId two = reg.getOrCreateArrayType(int32, {3, 4});
    EXPECT_TRUE(isMultiDimArray(two, reg));
    // Вложенный литерал `[[1,2],[3,4]]`: элемент — сам массив → многомерный.
    const TypeId inner = reg.getOrCreateArrayType(int32, {2});
    const TypeId nested = reg.getOrCreateArrayType(inner, {2});
    EXPECT_TRUE(isMultiDimArray(nested, reg));
    // Скаляр — не массив.
    EXPECT_FALSE(isMultiDimArray(int32, reg));
}

} // namespace
} // namespace trust
