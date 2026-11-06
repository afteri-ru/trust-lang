// test/unit/types/tensor_type_test.cpp
// Юнит-тесты Core-типа Tensor: предобъявление :Tensor/:SparseTensor, интернирование
// Tensor<Elem, shape>, аксессоры и алиас DenseTensor.

#include "support/cpp_facts_test_util.hpp"
#include "session/context.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"

#include "gtest/gtest.h"

#include <memory>

namespace trust {
namespace {

class TensorTypeFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
    TargetContract m_target;
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
        test::installTargetFacts(m_ctx, m_target);
    }
};

TEST_F(TensorTypeFixture, AbstractTypesRegistered) {
    TypeSystem& reg = *m_types;
    EXPECT_NE(reg.read().findType(type::Tensor), std::nullopt);
    EXPECT_NE(reg.read().findType(type::SparseTensor), std::nullopt);
    EXPECT_EQ(reg.read().store().desugaredTypeId(reg.read().getType(type::DenseTensor)), reg.read().getType(type::Tensor));
}

TEST_F(TensorTypeFixture, InterningByElementAndShape) {
    TypeSystem& reg = *m_types;
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId f64 = reg.read().getType(type::Float64);

    const TypeId t1 = reg.interner().getOrCreateTensorType(int32, {2, 3});
    const TypeId t2 = reg.interner().getOrCreateTensorType(int32, {2, 3});
    EXPECT_EQ(t1, t2);
    EXPECT_NE(reg.interner().getOrCreateTensorType(f64, {2, 3}), t1);
    EXPECT_NE(reg.interner().getOrCreateTensorType(int32, {3, 2}), t1);
    EXPECT_NE(reg.interner().getOrCreateTensorType(int32, {2, 3}, true), t1);
}

TEST_F(TensorTypeFixture, Accessors) {
    TypeSystem& reg = *m_types;
    const TypeId int64 = reg.read().getType(type::Int64);
    const TypeId dense = reg.interner().getOrCreateTensorType(int64, {4});
    ASSERT_NE(dense, INVALID_TYPE_ID);

    EXPECT_TRUE(reg.read().store().isTensorType(dense));
    EXPECT_FALSE(reg.read().store().isSparseTensorType(dense));
    EXPECT_EQ(reg.read().store().tensorElementType(dense), int64);
    EXPECT_EQ(reg.read().store().tensorDimensions(dense), (std::vector<uint64_t>{4}));

    const TypeId sparse = reg.interner().getOrCreateTensorType(int64, {4}, true);
    EXPECT_TRUE(reg.read().store().isTensorType(sparse));
    EXPECT_TRUE(reg.read().store().isSparseTensorType(sparse));

    // Не-тензор: безопасные значения без fallback-исключений.
    EXPECT_FALSE(reg.read().store().isTensorType(int64));
    EXPECT_EQ(reg.read().store().tensorElementType(int64), INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().store().tensorDimensions(int64).empty());
}

TEST_F(TensorTypeFixture, DynamicShapeAndProviderIncludes) {
    TypeSystem& reg = *m_types;
    const TypeId elem = reg.read().getType(type::Float32);
    const TypeId dyn = reg.interner().getOrCreateTensorType(elem, {});
    ASSERT_NE(dyn, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().store().tensorDimensions(dyn).empty());
    // Заголовок принадлежит провайдеру tensor.trust, а не структурному узлу.
    EXPECT_TRUE(test::cppFactsViewOf(m_ctx).preprocIncludeOf(dyn).empty());
    ASSERT_NE(test::defineBuiltinImplChecked(reg.read(), m_target, type::Tensor, "trust::Tensor", {"@stdlib/tensor.hpp"}, {"tensor"}), INVALID_TYPE_ID);
    EXPECT_FALSE(test::cppFactsViewOf(m_ctx).preprocIncludeOf(dyn).empty());
}

// Форма тензора входит в identity (TensorShapeTag): порядок размерностей значим,
// sparse/dense и динамическая форма - отдельные типы.
TEST_F(TensorTypeFixture, ShapeIsPartOfIdentity) {
    TypeSystem& reg = *m_types;
    const TypeId elem = reg.read().getType(type::Float32);
    const TypeId dense = reg.interner().getOrCreateTensorType(elem, {2, 3});
    EXPECT_EQ(reg.interner().getOrCreateTensorType(elem, {2, 3}), dense);
    EXPECT_NE(reg.interner().getOrCreateTensorType(elem, {3, 2}), dense);
    EXPECT_NE(reg.interner().getOrCreateTensorType(elem, {}), dense);
    EXPECT_NE(reg.interner().getOrCreateTensorType(elem, {2, 3}, true), dense);
}

} // namespace
} // namespace trust
