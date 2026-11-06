// test/unit/types/runtime_symbols_test.cpp
// Тесты единой таблицы рантайм-символов (target/runtime_symbols.hpp) и инварианта
// TypeSystem: рантайм-символы - только не-типовые функции; типы (Dict/Rational)
// не дублируются как символы (их заголовки подключаются по-типу).

#include "support/cpp_facts_test_util.hpp"
#include "types/type_system.hpp"
#include "target/runtime_symbols.hpp"
#include "types/names.hpp"
#include "session/context.hpp"
#include "gtest/gtest.h"

#include <set>
#include <string>

namespace trust {
namespace {

// Компайлтайм-инварианты таблицы: каждый реальный символ имеет непустые имя и заголовки,
// а имя не равно "trust::Dict" (тип, а не символ).
static_assert(runtimeSymbolName(RuntimeSymbolId::kTrustAbort) == "trust::trust__abort__");
static_assert(runtimeSymbolName(RuntimeSymbolId::kFormatMessage) == "trust::formatMessage");
static_assert(runtimeSymbolName(RuntimeSymbolId::kTrustPrint) == "trust::trust__print__");
static_assert(runtimeSymbolName(RuntimeSymbolId::kCheckedCast) == "trust::checked_cast");
static_assert(runtimeSymbolName(RuntimeSymbolId::kAnyTo) == "trust::any_to");
static_assert(runtimeSymbolSourceName(RuntimeSymbolId::kTrustPrint) == "trust__print__");
static_assert(runtimeSymbolSourceName(RuntimeSymbolId::kTrustAbort) == "trust__abort__");

TEST(RuntimeSymbolsTest, SourceAndCppNamesResolveIndependently) {
    // Источник-имя (`trust__print__` после `@print`) и C++-символ (`trust::trust__print__`) различны.
    EXPECT_EQ(findRuntimeSymbolBySourceName("trust__print__"), RuntimeSymbolId::kTrustPrint);
    EXPECT_EQ(findRuntimeSymbolByCppName("trust::trust__print__"), RuntimeSymbolId::kTrustPrint);
    EXPECT_EQ(findRuntimeSymbolByName("trust__print__"), RuntimeSymbolId::kTrustPrint);
    EXPECT_EQ(findRuntimeSymbolByName("trust::trust__print__"), RuntimeSymbolId::kTrustPrint);
    EXPECT_FALSE(findRuntimeSymbolBySourceName("trust::trust__print__").has_value());
    EXPECT_FALSE(findRuntimeSymbolByCppName("trust__print__").has_value());
    EXPECT_FALSE(findRuntimeSymbolByName("nope").has_value());
}

TEST(RuntimeSymbolsTest, TableNamesAreTypedAndUnique) {
    std::set<std::string> names;
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        const std::string_view name = runtimeSymbolName(id);
        // Каждый реальный символ - непустое имя с префиксом trust::.
        EXPECT_FALSE(name.empty());
        EXPECT_TRUE(name.starts_with("trust::"));
        // Имена уникальны (никакого дублирования строк).
        EXPECT_TRUE(names.insert(std::string(name)).second);
    }
}

TEST(RuntimeSymbolsTest, TableHeadersAreNonEmptyRuntimeDirectives) {
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        const auto headers = runtimeSymbolHeaders(id);
        EXPECT_FALSE(headers.empty());
        for (const auto h : headers) {
            // Заголовок - директива с маркером '@' (путь внутри пакета stdlib.trust).
            EXPECT_FALSE(h.empty());
            EXPECT_EQ(h.front(), '@');
        }
    }
}

class RuntimeSymbolsFixture : public ::testing::Test {
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

TEST_F(RuntimeSymbolsFixture, RegistryMatchesCompileTimeTable) {
    const auto& syms = runtimeSymbols();
    // Ровно kCount символов - все из компайлтайм-таблицы (регистрация циклом по kCount).
    ASSERT_EQ(syms.size(), static_cast<size_t>(RuntimeSymbolId::kCount));

    std::set<std::string> tableNames;
    for (size_t i = 0; i < static_cast<size_t>(RuntimeSymbolId::kCount); ++i) {
        tableNames.insert(std::string(runtimeSymbolName(static_cast<RuntimeSymbolId>(i))));
    }
    for (const auto& rs : syms) {
        EXPECT_TRUE(tableNames.count(rs.symbol));
        EXPECT_FALSE(rs.runtimeHeaders.empty());
    }
}

TEST_F(RuntimeSymbolsFixture, DictIsNotARuntimeSymbol) {
    // Инвариант: trust::Dict - ТИП, а не рантайм-символ (заголовки по-типу).
    const auto& syms = runtimeSymbols();
    for (const auto& rs : syms) {
        EXPECT_NE(rs.symbol, "trust::Dict");
    }
    // Но тип Dict зарегистрирован (предобъявлен); заголовок появляется только после загрузки
    // реализации из stdlib/builtin/dict.src (в ядре cppName/include больше нет).
    auto dictId = m_ctx.readTypes().findType(type::Dict);
    ASSERT_TRUE(dictId.has_value());
    EXPECT_TRUE(test::cppFactsViewOf(m_ctx).preprocIncludesOf(*dictId).empty());
    ASSERT_NE(test::defineBuiltinImplChecked(m_ctx.readTypes(), m_target, "Dict", "trust::Dict", {"@stdlib/builtin/dict.hpp"}, {}), INVALID_TYPE_ID);
    EXPECT_FALSE(test::cppFactsViewOf(m_ctx).preprocIncludesOf(*dictId).empty());
}

TEST_F(RuntimeSymbolsFixture, ResetRepopulatesSymbolsWithoutDuplicates) {
    // Фикс латентного бага: reset() не накапливает дубли m_runtimeSymbols.
    m_types->reset();
    const auto& syms = runtimeSymbols();
    ASSERT_EQ(syms.size(), static_cast<size_t>(RuntimeSymbolId::kCount));
}

} // namespace
} // namespace trust
