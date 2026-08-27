// test/unit/types/runtime_symbols_test.cpp
// Тесты единой таблицы рантайм-символов (types/runtime_symbols.hpp) и инварианта
// TypeRegistry: рантайм-символы - только не-типовые функции; типы (Dict/Rational)
// не дублируются как символы (их заголовки подключаются по-типу).

#include "types/registry.hpp"
#include "types/runtime_symbols.hpp"
#include "types/type_names.hpp"
#include "diag/context.hpp"
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
            // Заголовок - директива с маркером '@' (путь = ELF-секция trust-runtime).
            EXPECT_FALSE(h.empty());
            EXPECT_EQ(h.front(), '@');
        }
    }
}

class RuntimeSymbolsFixture : public ::testing::Test {
  protected:
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;

    void SetUp() override {
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }
};

TEST_F(RuntimeSymbolsFixture, RegistryMatchesCompileTimeTable) {
    const auto& syms = m_ctx.types().runtimeSymbols();
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
    const auto& syms = m_ctx.types().runtimeSymbols();
    for (const auto& rs : syms) {
        EXPECT_NE(rs.symbol, "trust::Dict");
    }
    // Но тип Dict зарегистрирован (и его заголовки доступны по-типу).
    auto dictId = m_ctx.types().findType(type::Dict);
    ASSERT_TRUE(dictId.has_value());
    EXPECT_FALSE(m_ctx.types().getPreprocIncludes(*dictId).empty());
}

TEST_F(RuntimeSymbolsFixture, ResetRepopulatesSymbolsWithoutDuplicates) {
    // Фикс латентного бага: reset() не накапливает дубли m_runtimeSymbols.
    m_ctx.types().reset();
    const auto& syms = m_ctx.types().runtimeSymbols();
    ASSERT_EQ(syms.size(), static_cast<size_t>(RuntimeSymbolId::kCount));
}

} // namespace
} // namespace trust
