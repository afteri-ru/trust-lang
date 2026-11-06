// test/unit/types/template_pack_test.cpp
// Type-packs: PackExpansionTypeData, TemplateBindings, унификация trailing pack,
// подстановка в одиночную позицию и splice в списковые аргументы.

#include "support/cpp_facts_test_util.hpp"
#include "types/type_system.hpp"
#include "types/subst/template_subst.hpp"
#include "types/call.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <vector>

namespace trust {
namespace {

class TemplatePackFixture : public ::testing::Test {
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

TEST_F(TemplatePackFixture, PackExpansionIsDistinctFromTemplateParam) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId pack = reg.interner().getOrCreatePackExpansionType(t);
    ASSERT_NE(pack, INVALID_TYPE_ID);
    EXPECT_TRUE(reg.read().store().isTemplateParamType(t));
    EXPECT_FALSE(reg.read().store().isPackExpansionType(t));
    EXPECT_TRUE(reg.read().store().isPackExpansionType(pack));
    EXPECT_FALSE(reg.read().store().isTemplateParamType(pack));
    EXPECT_NE(pack, t); // TypeKey различает форму (Group::kPackExpansion)
    EXPECT_EQ(reg.read().store().packExpansionPattern(pack), t);
    // Интернирование: та же форма → тот же TypeId.
    EXPECT_EQ(reg.interner().getOrCreatePackExpansionType(t), pack);
}

TEST_F(TemplatePackFixture, FunctionTypeDistinguishesPackParam) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId plain = reg.interner().getOrCreateFunctionType(int32, {int32});
    const TypeId withPack = reg.interner().getOrCreateFunctionType(int32, {}, INVALID_TYPE_ID, false, t, 1);
    EXPECT_NE(plain, withPack);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(withPack);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->paramPackType, t);
    EXPECT_EQ(fd->paramPackPosition, 1);
}

TEST_F(TemplatePackFixture, SubstitutionAndSplice) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId int64 = reg.read().getType(type::Int64);
    const TypeId pack = reg.interner().getOrCreatePackExpansionType(t);

    TemplateBindings bindings;
    bindings.push_back(TemplateBinding{t, true, {int32, int64}});

    // В одиночном контексте pack недопустим; splice — только через substituteTemplateArgList.
    EXPECT_EQ(substituteTemplateParams({reg.read(), reg.interner()}, bindings, t), INVALID_TYPE_ID);
    // Splice: T... -> [Int32, Int64].
    const auto spliced = substituteTemplateArgList({reg.read(), reg.interner()}, bindings, pack);
    ASSERT_EQ(spliced.size(), 2u);
    EXPECT_EQ(spliced[0], int32);
    EXPECT_EQ(spliced[1], int64);
}

TEST_F(TemplatePackFixture, UnifyNativeTemplateParameter) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId taskTpl = test::registerNativeTemplateFacts(reg, m_target, "TaskTpl", "trust::Task", {});
    const TypeId taskT = reg.interner().getOrCreateNativeTemplateType(taskTpl, {t});
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId taskInt = reg.interner().getOrCreateNativeTemplateType(taskTpl, {int32});

    TemplateDeduction ded;
    ASSERT_TRUE(unifyTemplateType(reg, taskT, taskInt, ded));
    ASSERT_EQ(ded.bindings.size(), 1u);
    EXPECT_EQ(ded.bindings[0].param, t);
    EXPECT_FALSE(ded.bindings[0].isPack);
    ASSERT_EQ(ded.bindings[0].types.size(), 1u);
    EXPECT_EQ(ded.bindings[0].types[0], int32);
}

TEST_F(TemplatePackFixture, UnifySignatureDeducesReturn) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId taskTpl = test::registerNativeTemplateFacts(reg, m_target, "TaskTpl", "trust::Task", {});
    const TypeId taskT = reg.interner().getOrCreateNativeTemplateType(taskTpl, {t});
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId taskInt = reg.interner().getOrCreateNativeTemplateType(taskTpl, {int32});
    const TypeId sig = reg.interner().getOrCreateFunctionType(t, {taskT});

    TemplateDeduction ded;
    const std::vector<TypeId> args{taskInt};
    ASSERT_TRUE(unifyTemplateSignature(reg, sig, args, ded));
    ASSERT_TRUE(ded.ok());
    const TypeId ret = substituteTemplateParams({reg.read(), reg.interner()}, ded.bindings, t);
    EXPECT_EQ(ret, int32);
}

TEST_F(TemplatePackFixture, UnboundReturnParameter) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId sig = reg.interner().getOrCreateFunctionType(t, {});
    TemplateDeduction ded;
    ASSERT_FALSE(unifyTemplateSignature(reg, sig, {}, ded));
    EXPECT_EQ(ded.status, TemplateDeduction::Status::Unbound);
    EXPECT_EQ(ded.conflictingParam, t);
}

TEST_F(TemplatePackFixture, UnifyTrailingPackZeroOneMany) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId int64 = reg.read().getType(type::Int64);
    // Возврат - не pack-параметр: единственная проверка - сбор pack-привязки.
    const TypeId sig = reg.interner().getOrCreateFunctionType(int32, {}, INVALID_TYPE_ID, false, t, 1);

    {
        TemplateDeduction ded;
        ASSERT_TRUE(unifyTemplateSignature(reg, sig, {}, ded));
        ASSERT_EQ(ded.bindings.size(), 1u);
        EXPECT_TRUE(ded.bindings[0].isPack);
        EXPECT_TRUE(ded.bindings[0].types.empty());
    }
    {
        TemplateDeduction ded;
        const std::vector<TypeId> args{int32};
        ASSERT_TRUE(unifyTemplateSignature(reg, sig, args, ded));
        ASSERT_EQ(ded.bindings.size(), 1u);
        EXPECT_TRUE(ded.bindings[0].isPack);
        ASSERT_EQ(ded.bindings[0].types.size(), 1u);
        EXPECT_EQ(ded.bindings[0].types[0], int32);
    }
    {
        TemplateDeduction ded;
        const std::vector<TypeId> args{int32, int64};
        ASSERT_TRUE(unifyTemplateSignature(reg, sig, args, ded));
        ASSERT_EQ(ded.bindings.size(), 1u);
        EXPECT_TRUE(ded.bindings[0].isPack);
        ASSERT_EQ(ded.bindings[0].types.size(), 2u);
        EXPECT_EQ(ded.bindings[0].types[0], int32);
        EXPECT_EQ(ded.bindings[0].types[1], int64);
    }
}

TEST_F(TemplatePackFixture, VariadicAndPackIncompatible) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId sig = reg.interner().getOrCreateFunctionType(int32, {}, int32, false, t, 1);
    TemplateDeduction ded;
    const std::vector<TypeId> args{int32};
    ASSERT_FALSE(unifyTemplateSignature(reg, sig, args, ded));
    EXPECT_EQ(ded.status, TemplateDeduction::Status::NotMatch);
}

TEST_F(TemplatePackFixture, SubstituteNativeTemplateSplice) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId int64 = reg.read().getType(type::Int64);
    const TypeId pack = reg.interner().getOrCreatePackExpansionType(t);
    const TypeId tupleTpl = test::registerNativeTemplateFacts(reg, m_target, "TupleTpl", "std::tuple", {}, "#include <tuple>");
    const TypeId tuple = reg.interner().getOrCreateNativeTemplateType(tupleTpl, {pack});

    TemplateBindings bindings;
    bindings.push_back(TemplateBinding{t, true, {int32, int64}});
    const TypeId sub = substituteTemplateParams({reg.read(), reg.interner()}, bindings, tuple);
    ASSERT_TRUE(reg.read().store().isNativeTemplateType(sub));
    EXPECT_EQ(test::cppFactsViewOf(m_ctx).nativeTemplateCppName(sub), "std::tuple");
    const auto& args = reg.read().store().nativeTemplateArgs(sub);
    ASSERT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0], int32);
    EXPECT_EQ(args[1], int64);

    // Пустой пак → пустой список типовых аргументов (`std::tuple<>`).
    bindings[0].types.clear();
    const TypeId empty = substituteTemplateParams({reg.read(), reg.interner()}, bindings, tuple);
    ASSERT_TRUE(reg.read().store().isNativeTemplateType(empty));
    EXPECT_TRUE(reg.read().store().nativeTemplateArgs(empty).empty());
}

TEST_F(TemplatePackFixture, SubstituteStructuralTupleElement) {
    TypeSystem& reg = *m_types;
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId int32 = reg.read().getType(type::Int32);
    const TypeId tuple = reg.interner().getOrCreateTupleType({{"", QualType::fromTypeId(t)}});

    TemplateBindings bindings;
    bindings.push_back(TemplateBinding{t, false, {int32}});
    const TypeId sub = substituteTemplateParams({reg.read(), reg.interner()}, bindings, tuple);
    ASSERT_NE(sub, INVALID_TYPE_ID);
    const auto* tt = reg.read().store().getTypeDataAs<TupleTypeData>(sub);
    ASSERT_NE(tt, nullptr);
    ASSERT_EQ(tt->elements.size(), 1u);
    EXPECT_EQ(tt->elements[0].type.getTypeId(), int32);
}

} // namespace
} // namespace trust
