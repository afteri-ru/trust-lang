// test/unit/types/overload_resolve_test.cpp
// ЕДИНЫЙ конвейер выбора перегрузки: OverloadCandidate + resolveCall/chooseBestOverload.
// Фиксирует variadic tie-break, Any-семантику, generic preference, defaults и effectiveSig.

#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/names.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <stdexcept>
#include <vector>

namespace trust {
namespace {

class OverloadResolveFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
    TypeId sig(std::vector<TypeId> params, TypeId ret) { return m_types->interner().getOrCreateFunctionType(ret, std::move(params)); }
    OverloadCandidate candidate(TypeId signature, std::vector<TypeId> args, bool templateAware = false, bool allowDefaults = false, std::size_t required = 0) {
        return OverloadCandidate{signature, std::move(args), required, templateAware, allowDefaults};
    }
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;
};

TEST_F(OverloadResolveFixture, VariadicExactBeatsNonVariadicConversion) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId any = reg.read().getType(type_generic::Any);
    const TypeId variadic = reg.interner().getOrCreateFunctionType(i32, {i32}, any);
    const TypeId fixed = sig({i64}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(variadic, {i32}), candidate(fixed, {i32})};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, variadic); // exact variadic wins over worse conversion
}

TEST_F(OverloadResolveFixture, NonVariadicWinsOnlyOnEqualRank) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId any = reg.read().getType(type_generic::Any);
    const TypeId variadic = reg.interner().getOrCreateFunctionType(i32, {i32}, any);
    const TypeId fixed = sig({i32}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(variadic, {i32}), candidate(fixed, {i32})};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, fixed); // equal rank -> non-variadic tie-break
}

TEST_F(OverloadResolveFixture, AnyParameterMatchesAnyArgumentExactly) {
    // `:Any` — КОНКРЕТНЫЙ тип: `Any -> Any` — exact, а `Any -> Int32` нежизнеспособен
    // (у std::any нет неявной конверсии в другой тип).
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId any = reg.read().getType(type_generic::Any);
    const TypeId sigAny = sig({any}, i32);
    const TypeId sigI32 = sig({i32}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(sigAny, {any}), candidate(sigI32, {any})};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, sigAny);
    EXPECT_TRUE(r.ambiguous.empty());
}

TEST_F(OverloadResolveFixture, AnyArgumentIsNotConvertibleToConcreteParameter) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId any = reg.read().getType(type_generic::Any);
    const TypeId signature = sig({i32}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(signature, {any})};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    EXPECT_EQ(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.failure, OverloadResolution::Failure::NoMatch);
    EXPECT_LT(overloadConversionRank(reg, any, i32), 0);
}

TEST_F(OverloadResolveFixture, InvalidArgumentStaysViable) {
    // Неизвестный тип аргумента (ошибка уже выдана выше) не должен давать каскад:
    // кандидат остаётся жизнеспособным с низшим рангом.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId signature = sig({i32}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(signature, {INVALID_TYPE_ID})};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    EXPECT_EQ(r.chosen, signature);
    EXPECT_EQ(r.failure, OverloadResolution::Failure::None);
}

TEST_F(OverloadResolveFixture, AnyDoesNotMatchStructuredParameter) {
    // `:Any` — конкретный тип, а `Array<T>` — структурный: аргумент Any не подходит по форме
    // (NotMatch).
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId any = reg.read().getType(type_generic::Any);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId arrayT = reg.interner().getOrCreateArrayType(t);
    const TypeId generic = reg.interner().getOrCreateFunctionType(i32, {arrayT});
    const std::vector<OverloadCandidate> candidates{candidate(generic, {any}, /*templateAware=*/true)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    EXPECT_EQ(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.failure, OverloadResolution::Failure::NoMatch);
}

TEST_F(OverloadResolveFixture, BareTemplateParamFromAnyIsDeduced) {
    // `:Any` — известный конкретный тип, поэтому `T := Any` допустим (C++-модель вывода).
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId any = reg.read().getType(type_generic::Any);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId generic = reg.interner().getOrCreateFunctionType(t, {t});
    const std::vector<OverloadCandidate> candidates{candidate(generic, {any}, /*templateAware=*/true)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, generic);
    ASSERT_NE(r.effectiveSig, INVALID_TYPE_ID);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(r.effectiveSig);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->paramTypes, (std::vector<TypeId>{any}));
    EXPECT_EQ(fd->returnType, any);
    std::vector<TypeId> remaining;
    collectTemplateParams(reg, r.effectiveSig, remaining);
    EXPECT_TRUE(remaining.empty());
}

TEST_F(OverloadResolveFixture, NonFunctionCandidateIsAnInternalError) {
    // Кандидат обязан быть функциональным типом: молчаливый пропуск маскировал бы
    // внутреннюю ошибку под пользовательское «no matching overload».
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const std::vector<OverloadCandidate> candidates{candidate(i32, {i32})};
    EXPECT_THROW(resolveCall({reg.read(), reg.interner()}, candidates), std::runtime_error);
}

TEST_F(OverloadResolveFixture, GenericDeductionProducesEffectiveSignature) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId generic = reg.interner().getOrCreateFunctionType(t, {t});
    const std::vector<OverloadCandidate> candidates{candidate(generic, {i32}, /*templateAware=*/true)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, generic);
    ASSERT_NE(r.effectiveSig, INVALID_TYPE_ID);
    EXPECT_NE(r.effectiveSig, generic);
    ASSERT_FALSE(r.bindings.empty());
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(r.effectiveSig);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->returnType, i32);
    EXPECT_EQ(fd->paramTypes, std::vector<TypeId>{i32});
}

TEST_F(OverloadResolveFixture, NonGenericPreferredOverTemplate) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId generic = reg.interner().getOrCreateFunctionType(t, {t});
    const TypeId concrete = sig({i32}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(generic, {i32}, /*templateAware=*/true), candidate(concrete, {i32}, /*templateAware=*/true)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, concrete);
}

TEST_F(OverloadResolveFixture, BoundDefaultsAllowTailOmission) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId two = sig({i32, i32}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(two, {i32}, /*templateAware=*/false, /*allowDefaults=*/true, 1)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, two);
    EXPECT_EQ(r.effectiveSig, two);
}

TEST_F(OverloadResolveFixture, TypePackIsExpandedInEffectiveSignature) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId str = reg.read().getType(type::StrChar);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId pack = reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1);
    const std::vector<OverloadCandidate> candidates{candidate(pack, {i32, str}, /*templateAware=*/true)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    ASSERT_NE(r.effectiveSig, INVALID_TYPE_ID);
    const auto* fd = reg.read().store().getTypeDataAs<FunctionTypeData>(r.effectiveSig);
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->paramTypes, (std::vector<TypeId>{i32, str}));
    EXPECT_EQ(fd->paramPackType, INVALID_TYPE_ID);
}

TEST_F(OverloadResolveFixture, NoMatchOnWrongArity) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId two = sig({i32, i32}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(two, {i32})};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    EXPECT_EQ(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.failure, OverloadResolution::Failure::NoMatch);
}

TEST_F(OverloadResolveFixture, UserAliasIsConversionNotExactRank) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId alias = reg.registrar().registerType("MyInt", i32);
    ASSERT_NE(alias, INVALID_TYPE_ID);
    // Пользовательский алиас - отдельный тип Trust: не exact, но числовая конверсия разрешена.
    EXPECT_GT(overloadConversionRank(reg, alias, i32), 0);
    EXPECT_GT(overloadConversionRank(reg, i32, alias), 0);
    const TypeId signature = sig({i32}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(signature, {alias})};
    EXPECT_EQ(resolveCall({reg.read(), reg.interner()}, candidates).chosen, signature);
    // Встроенный representation-алиас прозрачен -> exact.
    const auto integer = reg.read().findType("Integer");
    ASSERT_TRUE(integer.has_value());
    EXPECT_EQ(overloadConversionRank(reg, *integer, reg.read().getType(type::Int64)), 0);
}

TEST_F(OverloadResolveFixture, PackCandidateRequiresTemplateAware) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId pack = reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1);
    const std::vector<OverloadCandidate> candidates{candidate(pack, {i32}, /*templateAware=*/false)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    EXPECT_EQ(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.failure, OverloadResolution::Failure::PackWithoutTemplate);
}

TEST_F(OverloadResolveFixture, PackInReturnPositionIsReturnPackFailure) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId str = reg.read().getType(type::StrChar);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    // f<T...>(...) -> T : a pack must not be expanded into a single-type return position.
    const TypeId sig = reg.interner().getOrCreateFunctionType(t, {i32}, INVALID_TYPE_ID, false, t, 1);
    const std::vector<OverloadCandidate> candidates{candidate(sig, {i32, str}, /*templateAware=*/true)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    // `chosen` may be set while the EFFECTIVE signature cannot be built; the failure names the cause.
    EXPECT_EQ(r.effectiveSig, INVALID_TYPE_ID);
    EXPECT_EQ(r.failure, OverloadResolution::Failure::ReturnPack);
}

TEST_F(OverloadResolveFixture, IncomparableGenericAndNonGenericIsAmbiguous) {
    // C++ [over.match.best]: a non-template is preferred only when its conversion sequences are
    // not worse. Here the generic is exact on arg 0 and the concrete one on arg 1 -> ambiguous.
    TypeSystem& reg = *m_types;
    const TypeId i8 = reg.read().getType(type::Int8);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId generic = reg.interner().getOrCreateFunctionType(i8, {t, i64});
    const TypeId concrete = reg.interner().getOrCreateFunctionType(i8, {i64, i8});
    const std::vector<OverloadCandidate> candidates{candidate(generic, {i8, i8}, /*templateAware=*/true),
                                                    candidate(concrete, {i8, i8}, /*templateAware=*/true)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    EXPECT_EQ(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.ambiguous.size(), 2u);
}

TEST_F(OverloadResolveFixture, RankVectorsOfDifferentLengthAreRejected) {
    // Инвариант единого конвейера: все кандидаты вызова подготовлены по ОДНОМУ набору
    // фактических аргументов -> одинаковая длина ranks. Разные длины - внутренняя ошибка,
    // а не «тихая несравнимость» (иначе будущие формы давали бы ложную ambiguous).
    const TypeId signature = m_types->read().getType(type::Int32);
    std::vector<OverloadRankedCandidate> candidates{
        OverloadRankedCandidate{signature, {0}, 0, false, 0},
        OverloadRankedCandidate{signature, {0, 0}, 0, false, 1},
    };
    EXPECT_THROW(chooseBestOverload(std::move(candidates)), std::runtime_error);
}

TEST_F(OverloadResolveFixture, FixedArityBeatsPackAtEqualRank) {
    // C++ partial ordering: при равных последовательностях конверсий `f(T)` специфичнее
    // `f(T...)` (pack - не-фиксированный слот).
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId pack = reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1);
    const TypeId fixed = reg.interner().getOrCreateFunctionType(i32, {t});
    const std::vector<OverloadCandidate> candidates{candidate(pack, {i32}, /*templateAware=*/true), candidate(fixed, {i32}, /*templateAware=*/true)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, fixed);
    EXPECT_TRUE(r.ambiguous.empty());
}

TEST_F(OverloadResolveFixture, PackExactBeatsFixedWorseConversion) {
    // Штраф за не-фиксированный слот применяется ТОЛЬКО при полном равенстве рангов:
    // exact pack обязан выигрывать у fixed с худшей конверсией.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId pack = reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1);
    const TypeId fixed = reg.interner().getOrCreateFunctionType(i32, {i64});
    const std::vector<OverloadCandidate> candidates{candidate(pack, {i32}, /*templateAware=*/true), candidate(fixed, {i32}, /*templateAware=*/true)};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, pack);
    EXPECT_TRUE(r.ambiguous.empty());
}

TEST_F(OverloadResolveFixture, MixedPackAndMismatchedConcreteReportsNoMatch) {
    // A concrete candidate was actually considered and rejected (arity): the accurate diagnosis is
    // NoMatch, not PackWithoutTemplate.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId pack = reg.interner().getOrCreateFunctionType(i32, {}, INVALID_TYPE_ID, false, t, 1);
    const TypeId concrete = sig({i32, i32}, i32);
    const std::vector<OverloadCandidate> candidates{candidate(pack, {i32}, /*templateAware=*/false), candidate(concrete, {i32})};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    EXPECT_EQ(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.failure, OverloadResolution::Failure::NoMatch);
}

TEST_F(OverloadResolveFixture, MemberCandidateRanksImplicitObjectArgument) {
    // member и free формы ранжируются в ОДНОМ наборе: неявный object-параметр (`objectOwner`)
    // даёт ведущий слот, объявленные параметры выровнены с `argTypes[1..]`.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId memberSig = sig({i64}, i32);    // member: (o: Int64)
    const TypeId freeSig = sig({i32, i32}, i32); // free:   (a: Int32, b: Int32)
    OverloadCandidate member{memberSig, {i32, i32}, 0, false, false, i32};
    OverloadCandidate freeCand{freeSig, {i32, i32}, 0, false, false};
    const std::vector<OverloadCandidate> candidates{member, freeCand};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, freeSig); // exact/exact лучше exact/widening (C++ [over.match.best])
    EXPECT_EQ(r.chosenIndex, 1u);
}

TEST_F(OverloadResolveFixture, MemberCandidateArityIncludesImplicitObject) {
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId memberSig = sig({i32}, i32); // один объявленный параметр
    const std::vector<OverloadCandidate> withObject{OverloadCandidate{memberSig, {i32, i32}, 0, false, false, i32}};
    EXPECT_EQ(resolveCall({reg.read(), reg.interner()}, withObject).chosen, memberSig); // receiver + параметр
    // Без objectOwner та же сигнатура нежизнеспособна: арность не сходится.
    const std::vector<OverloadCandidate> withoutObject{OverloadCandidate{memberSig, {i32, i32}, 0, false, false}};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, withoutObject);
    EXPECT_EQ(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.failure, OverloadResolution::Failure::NoMatch);
}

TEST_F(OverloadResolveFixture, MemberCandidatesWithEqualRanksAreAmbiguous) {
    // Две несравнимые перегрузки одной (member) формы - амбигуа ранжирования.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId i64 = reg.read().getType(type::Int64);
    const TypeId u64 = reg.read().getType(type::UInt64);
    const std::vector<OverloadCandidate> candidates{OverloadCandidate{sig({i64}, i32), {i32, i32}, 0, false, false, i32},
                                                    OverloadCandidate{sig({u64}, i32), {i32, i32}, 0, false, false, i32}};
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    EXPECT_EQ(r.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(r.ambiguous.size(), 2u);
}

TEST_F(OverloadResolveFixture, SelectCallCandidateIsReadOnly) {
    // DoD-тест: read-only селектор БЕЗ StructuralInterner и БЕЗ интернирования — число
    // пользовательских дескрипторов не растёт за время `selectCallCandidate`.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId genericSig = sig({t}, i32);    // f(x: T) -> Int32
    const TypeId concreteSig = sig({i32}, i32); // f(x: Int32) -> Int32
    const std::vector<OverloadCandidate> candidates{
        candidate(genericSig, {i32}, /*templateAware=*/true),
        candidate(concreteSig, {i32}),
    };
    const std::size_t before = reg.read().store().userTypeCount();
    const OverloadResolution sel = selectCallCandidate(reg.read(), candidates);
    const std::size_t after = reg.read().store().userTypeCount();
    EXPECT_EQ(after, before) << "selectCallCandidate must be read-only (no interning)";
    ASSERT_NE(sel.chosen, INVALID_TYPE_ID);
    // Выбор согласуется с полным конвейером resolveCall; инстанцирование effectiveSig —
    // только resolveCall.
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    EXPECT_EQ(r.chosen, sel.chosen);
    EXPECT_EQ(r.chosenIndex, sel.chosenIndex);
    EXPECT_NE(r.effectiveSig, INVALID_TYPE_ID);
}

TEST_F(OverloadResolveFixture, SelectCompositeTemplateWithoutInterning) {
    // Составная шаблонная позиция (`Array<T>`): успешная унификация уже гарантирует точное
    // равенство подставленного типа аргументу -> ранг exact, интернирования на этапе выбора нет.
    TypeSystem& reg = *m_types;
    const TypeId i32 = reg.read().getType(type::Int32);
    const TypeId t = reg.interner().getOrCreateTemplateParamType("T");
    const TypeId arrT = reg.interner().getOrCreateArrayType(t);
    const TypeId arrI32 = reg.interner().getOrCreateArrayType(i32);
    const TypeId genericSig = sig({arrT}, i32); // f(xs: Array<T>) -> Int32
    const std::vector<OverloadCandidate> candidates{
        candidate(genericSig, {arrI32}, /*templateAware=*/true),
    };
    const std::size_t before = reg.read().store().userTypeCount();
    const OverloadResolution sel = selectCallCandidate(reg.read(), candidates);
    EXPECT_EQ(reg.read().store().userTypeCount(), before);
    ASSERT_NE(sel.chosen, INVALID_TYPE_ID);
    EXPECT_EQ(sel.chosen, genericSig);
    // Эффективная сигнатура строится отдельным шагом (resolveCall).
    const OverloadResolution r = resolveCall({reg.read(), reg.interner()}, candidates);
    ASSERT_NE(r.effectiveSig, INVALID_TYPE_ID);
    EXPECT_EQ(r.chosen, sel.chosen);
}
} // namespace
} // namespace trust
