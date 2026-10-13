// test/unit/types/template_unify_test.cpp
// Унификация типовых параметров: единый исчерпывающий обход TypeData (type_traversal.hpp).
// Регрессия: Tensor<T> и ref-поля (deleter/policy/impl) обязаны участвовать в выводе T.

#include "types/type_system.hpp"
#include "types/call.hpp"
#include "types/names.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <vector>

namespace trust {
namespace {

class TemplateUnifyFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(TemplateUnifyFixture, TensorElementDeducesTemplateParam) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId tensorT = reg.interner().getOrCreateTensorType(t, {2, 3}, /*sparse=*/false);
    const TypeId tensorI32 = reg.interner().getOrCreateTensorType(i32, {2, 3}, /*sparse=*/false);

    TemplateDeduction ded;
    ASSERT_TRUE(unifyTemplateType(reg, tensorT, tensorI32, ded));
    ASSERT_TRUE(ded.hasBinding(t));
    EXPECT_EQ(ded.bindings.front().types.front(), i32);
}

TEST_F(TemplateUnifyFixture, TensorShapeOrStorageMismatchIsNotMatch) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId tensorT = reg.interner().getOrCreateTensorType(t, {2, 3}, false);
    const TypeId otherShape = reg.interner().getOrCreateTensorType(i32, {2, 4}, false);
    const TypeId sparse = reg.interner().getOrCreateTensorType(i32, {2, 3}, true);

    TemplateDeduction dedShape;
    EXPECT_FALSE(unifyTemplateType(reg, tensorT, otherShape, dedShape));
    EXPECT_EQ(dedShape.status, TemplateDeduction::Status::NotMatch);

    TemplateDeduction dedSparse;
    EXPECT_FALSE(unifyTemplateType(reg, tensorT, sparse, dedSparse));
    EXPECT_EQ(dedSparse.status, TemplateDeduction::Status::NotMatch);
}

TEST_F(TemplateUnifyFixture, RefDeleterDeducesTemplateParam) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId d = reg.interner().getOrCreateTemplateParamType("D");
    const TypeId uniqueT = reg.interner().getOrCreateRefType(RefType::kUnique, t, d);
    const TypeId uniqueI = reg.interner().getOrCreateRefType(RefType::kUnique, i32, i64);

    TemplateDeduction ded;
    ASSERT_TRUE(unifyTemplateType(reg, uniqueT, uniqueI, ded));
    ASSERT_TRUE(ded.hasBinding(t));
    ASSERT_TRUE(ded.hasBinding(d));
    EXPECT_EQ(ded.bindings[0].types.front(), i32);
    EXPECT_EQ(ded.bindings[1].types.front(), i64);
}

TEST_F(TemplateUnifyFixture, RefFastPathAndStructuralNodeAreEquivalent) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    // Fast-path ref-бит (без RefTypeData) против структурного узла с INVALID aux-полями:
    // один и тот же C++-тип, вывод T не должен ломаться на отсутствии данных.
    const TypeId fast = reg.interner().applyRefType(t, RefType::kShared);
    const TypeId node = reg.interner().getOrCreateRefType(RefType::kShared, i32);

    TemplateDeduction ded;
    ASSERT_TRUE(unifyTemplateType(reg, fast, node, ded));
    ASSERT_TRUE(ded.hasBinding(t));
    EXPECT_EQ(ded.bindings.front().types.front(), i32);
}

TEST_F(TemplateUnifyFixture, CollectTraversesRefPointeeNotTheRefItself) {
    TypeSystem& reg = m_ctx.types();
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId sharedT = reg.interner().applyRefType(t, RefType::kShared);
    std::vector<TypeId> params;
    collectTemplateParams(reg, sharedT, params);
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params.front(), t);
}

TEST_F(TemplateUnifyFixture, CollectTraversesTensorInsideSignature) {
    TypeSystem& reg = m_ctx.types();
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId tensorT = reg.interner().getOrCreateTensorType(t, {4}, false);
    const TypeId sig = reg.interner().getOrCreateFunctionType(i32, {tensorT});

    std::vector<TypeId> params;
    collectTemplateParams(reg, sig, params);
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params.front(), t);
}

} // namespace
} // namespace trust
