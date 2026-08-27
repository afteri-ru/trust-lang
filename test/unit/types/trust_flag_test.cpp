// test/unit/types/trust_flag_test.cpp
// Юнит-тесты trust-флага в TypeId/TypeKind (семантический дифференциатор идентичности типа:
// тип/функция с trust-условиями не эквивалентен идентичному без условий) и разбора поведенческого
// режима --solver-mode (semantic/solver.hpp).

#include "semantic/solver.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/intrinsics.hpp"
#include "diag/context.hpp"
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
    EXPECT_FALSE(typeIsTrusted(INVALID_TYPE_ID));
    EXPECT_FALSE(typeIsTrusted(makeTypeId(makeTypeKind(Group::kCallable, 1))));

    // Установка бита и его проверка.
    const TypeId trusted = withTrusted(makeTypeId(makeTypeKind(Group::kCallable, 1), 7));
    EXPECT_TRUE(typeIsTrusted(trusted));
    EXPECT_FALSE(typeIsTrusted(makeTypeId(makeTypeKind(Group::kCallable, 1), 7)));

    // Бит в верхней половине (TypeKind) НЕ снимается канонизацией нижних квалификаторов:
    // getIndexFromId маскирует kInferred/kConst (нижняя половина), но не trust.
    EXPECT_EQ(getIndexFromId(trusted), 7u);
    EXPECT_TRUE(typeIsTrusted(trusted));

    // Ортогональность к нижним квалификаторам: trust сохраняется при withConst/withInferred.
    const TypeId combined = withConst(withInferred(trusted));
    EXPECT_TRUE(typeIsTrusted(combined));
    EXPECT_TRUE(typeIsConst(combined));
    EXPECT_TRUE(typeIsInferred(combined));
}

class TrustRegistryFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;

    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(TrustRegistryFixture, RegisterAliasWithTrust) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId plain = reg.registerType("Plain", reg.getType("Int32"), {}, {});
    ASSERT_NE(plain, INVALID_TYPE_ID);
    const TypeId trusted = reg.registerType("Trusted", reg.getType("Int32"), {}, {}, "", /*hasTrust=*/true);
    ASSERT_NE(trusted, INVALID_TYPE_ID);

    // Обычный алиас не доверенный, алиас с trust - доверенный (разные TypeId).
    EXPECT_FALSE(typeIsTrusted(plain));
    EXPECT_TRUE(typeIsTrusted(trusted));
    EXPECT_NE(plain, trusted);
}

TEST_F(TrustRegistryFixture, FunctionWithTrustDistinctFromPlain) {
    TypeRegistry& reg = m_ctx.types();
    const TypeId ret = reg.getType("Int32");
    const TypeId fPlain = reg.getOrCreateFunctionType(ret, {ret});
    const TypeId fTrusted = reg.getOrCreateFunctionType(ret, {ret}, INVALID_TYPE_ID, /*hasTrust=*/true);

    // Функция с trust-условиями - ОТДЕЛЬНЫЙ тип от идентичной сигнатуры без условий.
    EXPECT_NE(fPlain, fTrusted);
    EXPECT_TRUE(typeIsTrusted(fTrusted));
    EXPECT_FALSE(typeIsTrusted(fPlain));

    // Интернирование: повторный запрос той же сигнатуры/того же признака даёт тот же TypeId.
    EXPECT_EQ(fPlain, reg.getOrCreateFunctionType(ret, {ret}));
    EXPECT_EQ(fTrusted, reg.getOrCreateFunctionType(ret, {ret}, INVALID_TYPE_ID, true));
}

TEST(SolverMode, ParseValues) {
    using semantic::SolverMode;
    // Поведенческие режимы (--solver-mode): assert/export/calculate.
    EXPECT_EQ(semantic::parseSolverMode("assert"), SolverMode::kAssert);
    EXPECT_EQ(semantic::parseSolverMode("export"), SolverMode::kExport);
    EXPECT_EQ(semantic::parseSolverMode("calculate"), SolverMode::kCalculate);
    // severity-значения больше НЕ поведенческие режимы (теперь это severity-опция -Wsolver).
    EXPECT_EQ(semantic::parseSolverMode("ignore"), std::nullopt);
    EXPECT_EQ(semantic::parseSolverMode("warning"), std::nullopt);
    EXPECT_EQ(semantic::parseSolverMode("error"), std::nullopt);
    EXPECT_EQ(semantic::parseSolverMode("bogus"), std::nullopt);
    EXPECT_EQ(semantic::parseSolverMode(""), std::nullopt);
}

TEST(SolverMode, Names) {
    using semantic::SolverMode;
    EXPECT_EQ(semantic::solverModeName(SolverMode::kAssert), "assert");
    EXPECT_EQ(semantic::solverModeName(SolverMode::kExport), "export");
    EXPECT_EQ(semantic::solverModeName(SolverMode::kCalculate), "calculate");
}

TEST_F(TrustRegistryFixture, SolverSeverityAndMode) {
    // -Wsolver - severity-диагностика «присутствуют trust-условия» (default warning).
    EXPECT_EQ(m_ctx.opts().get(semantic::DiagId::Solver), Severity::Warning);
    // -Wsolver=ignore глушит presence-диагностику (nullopt = ignore).
    m_ctx.opts().set(semantic::DiagId::Solver, std::nullopt);
    EXPECT_FALSE(m_ctx.opts().get(semantic::DiagId::Solver).has_value());
    // --solver-mode - поведенческий флаг; не задан -> nullopt (никакое поведение).
    EXPECT_FALSE(semantic::solverModeFromOptions(m_ctx.opts()).has_value());
    m_ctx.opts().set_flag_value(semantic::FlagKind::SolverMode, "assert");
    EXPECT_EQ(semantic::solverModeFromOptions(m_ctx.opts()), semantic::SolverMode::kAssert);
    EXPECT_TRUE(semantic::solverAssertEnabled(m_ctx.opts()));
}

} // namespace
} // namespace trust