// test/unit/types/trust_flag_test.cpp
// Юнит-тесты trust-флага в TypeId/TypeKind (семантический дифференциатор идентичности типа:
// тип/функция с trust-условиями не эквивалентен идентичному без условий) и разбора поведенческого
// режима --solver-mode (semantic/solver.hpp).

#include "semantic/solver.hpp"
#include "type_flag_test_utils.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/intrinsics.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <memory>
#include <string_view>

namespace trust {
namespace {

TEST(Intrinsics, Registry) {
    // Реестр интринсиков: имя, поиск по точному имени (без '%' - интринсик не нативная функция),
    // заголовки.
    EXPECT_EQ(intrinsicName(IntrinsicId::kTrustAssert), "trust::intrinsic_assert");
    EXPECT_EQ(findIntrinsicByName("trust::intrinsic_assert"), IntrinsicId::kTrustAssert);
    EXPECT_EQ(findIntrinsicByName("trust::intrinsic_nope"), std::nullopt);
    EXPECT_EQ(findIntrinsicByName("trust::trust__abort__"), std::nullopt); // интринсик ≠ рантайм-функция
    EXPECT_FALSE(intrinsicHeaders(IntrinsicId::kTrustAssert).empty());
}

TEST(TrustFlag, IsTrustedBits) {
    // По умолчанию признак отсутствует.
    EXPECT_FALSE(isTrustedType(INVALID_TYPE_ID));
    EXPECT_FALSE(isTrustedType(makeTypeId(makeTypeKind(Group::kCallable, 1))));

    // Установка бита и его проверка.
    const TypeId base = makeTypeId(makeTypeKind(Group::kCallable, 1), 7);
    const TypeId trusted = QualType::fromTypeId(base).withKind(setTrustFlag(getTypeKind(base))).getTypeId();
    EXPECT_TRUE(isTrustedType(trusted));
    EXPECT_FALSE(isTrustedType(makeTypeId(makeTypeKind(Group::kCallable, 1), 7)));

    // Бит в верхней половине (TypeKind) НЕ снимается канонизацией нижних квалификаторов:
    // getRegistryIndex маскирует kInferred/kConst (нижняя половина), но не trust.
    EXPECT_EQ(getRegistryIndex(trusted), 7u);
    EXPECT_TRUE(isTrustedType(trusted));

    // Ортогональность к нижним квалификаторам: trust сохраняется при withConst/withInferred.
    const TypeId combined = setFlag(setFlag(trusted, TypeFlag::Inferred), TypeFlag::Const);
    EXPECT_TRUE(isTrustedType(combined));
    EXPECT_TRUE(testFlag(combined, TypeFlag::Const));
    EXPECT_TRUE(testFlag(combined, TypeFlag::Inferred));
}

class TrustRegistryFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeSystem> m_types;

    void SetUp() override {
        m_types = std::make_unique<TypeSystem>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(TrustRegistryFixture, RegisterAliasWithTrust) {
    TypeSystem& reg = m_ctx.types();
    const TypeId plain = reg.registrar().registerType("Plain", reg.read().getType("Int32"), {}, {});
    ASSERT_NE(plain, INVALID_TYPE_ID);
    const TypeId trusted = reg.registrar().registerType("Trusted", reg.read().getType("Int32"), {}, {}, "", /*hasTrust=*/true);
    ASSERT_NE(trusted, INVALID_TYPE_ID);

    // Обычный алиас не доверенный, алиас с trust - доверенный (разные TypeId).
    EXPECT_FALSE(isTrustedType(plain));
    EXPECT_TRUE(isTrustedType(trusted));
    EXPECT_NE(plain, trusted);
}

TEST_F(TrustRegistryFixture, FunctionWithTrustDistinctFromPlain) {
    TypeSystem& reg = m_ctx.types();
    const TypeId ret = reg.read().getType("Int32");
    const TypeId fPlain = reg.interner().getOrCreateFunctionType(ret, {ret});
    const TypeId fTrusted = reg.interner().getOrCreateFunctionType(ret, {ret}, INVALID_TYPE_ID, /*hasTrust=*/true);

    // Функция с trust-условиями - ОТДЕЛЬНЫЙ тип от идентичной сигнатуры без условий.
    EXPECT_NE(fPlain, fTrusted);
    EXPECT_TRUE(isTrustedType(fTrusted));
    EXPECT_FALSE(isTrustedType(fPlain));

    // Интернирование: повторный запрос той же сигнатуры/того же признака даёт тот же TypeId.
    EXPECT_EQ(fPlain, reg.interner().getOrCreateFunctionType(ret, {ret}));
    EXPECT_EQ(fTrusted, reg.interner().getOrCreateFunctionType(ret, {ret}, INVALID_TYPE_ID, true));
}

TEST(SolverMode, ParseValues) {
    using analysis::SolverMode;
    // Поведенческие режимы (--solver-mode): assert/export/calculate.
    EXPECT_EQ(analysis::parseSolverMode("assert"), SolverMode::kAssert);
    EXPECT_EQ(analysis::parseSolverMode("export"), SolverMode::kExport);
    EXPECT_EQ(analysis::parseSolverMode("calculate"), SolverMode::kCalculate);
    // severity-значения больше НЕ поведенческие режимы (теперь это severity-опция -Wsolver).
    EXPECT_EQ(analysis::parseSolverMode("ignore"), std::nullopt);
    EXPECT_EQ(analysis::parseSolverMode("warning"), std::nullopt);
    EXPECT_EQ(analysis::parseSolverMode("error"), std::nullopt);
    EXPECT_EQ(analysis::parseSolverMode("bogus"), std::nullopt);
    EXPECT_EQ(analysis::parseSolverMode(""), std::nullopt);
}

TEST(SolverMode, Names) {
    using analysis::SolverMode;
    EXPECT_EQ(analysis::solverModeName(SolverMode::kAssert), "assert");
    EXPECT_EQ(analysis::solverModeName(SolverMode::kExport), "export");
    EXPECT_EQ(analysis::solverModeName(SolverMode::kCalculate), "calculate");
}

TEST_F(TrustRegistryFixture, SolverSeverityAndMode) {
    // -Wsolver - severity-диагностика «присутствуют trust-условия» (default warning).
    EXPECT_EQ(m_ctx.opts().get(semantic::DiagId::Solver), Severity::Warning);
    // -Wsolver=ignore глушит presence-диагностику (Severity::Ignore = выключено).
    m_ctx.opts().set(semantic::DiagId::Solver, Severity::Ignore);
    EXPECT_EQ(m_ctx.opts().get(semantic::DiagId::Solver), Severity::Ignore);
    // --solver-mode - поведенческий флаг; не задан -> nullopt (никакое поведение).
    EXPECT_FALSE(semantic::solverModeFromOptions(m_ctx.opts()).has_value());
    m_ctx.opts().set_flag_value(semantic::FlagKind::SolverMode, "assert");
    EXPECT_EQ(semantic::solverModeFromOptions(m_ctx.opts()), analysis::SolverMode::kAssert);
    EXPECT_TRUE(semantic::solverAssertEnabled(m_ctx.opts()));
}

} // namespace
} // namespace trust